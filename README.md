<p align="center">
    <img src="icons/titleimage.png" alt="titleimage" width="600";>
</p>


# komapedit

[简体中文Readme](README_zhcn.md)
## Project Overview

komapedit is a lightweight viewer and editor for BVE Trainsim map files. It reworks the track-geometry approach from `kobushi-trackviewer` as a C++/Win32 desktop application. The current version focuses on loading maps, generating track geometry, showing 2D plan/profile/radius views, displaying map data tables, inspecting source/include files, editing a limited set of source-backed map elements, previewing Structure models and map scenes in 3D, and exporting track geometry to CSV.

The application has three main runtime components:

- `maploader.dll`: loads `BveTs Map` files, parses part of the BVE Map syntax, generates own-track and other-track geometry, and exposes versioned typed snapshots through a fixed-width C ABI.
- `model_loader.dll`: loads Structure model files through Assimp and exposes mesh/material data for the 3D previews.
- `komapedit.exe`: the desktop GUI, built with Dear ImGui, ImPlot, Win32, DirectX 11, and WIC.

The bundled executable and `maploader.dll` use maploader API v6. When edit metadata is requested, `KvMapSnapshot` v6 carries map, regular-geometry, source, and edit metadata, including one typed logical row for each supported other-track change statement; the independently invalidated `KvSceneGeometrySnapshot` v1 carries dense 3D own/other-track geometry. Typed edit batches and handle-owned typed reports cover dry-run, in-memory Apply, direct Apply, Save/commit, and target lookup. These views are process-memory only: Open and Reload always read the current route sources, and no route snapshot or geometry cache is written to disk.

At this stage, komapedit provides source-backed editing for existing list rows referenced by `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load`; supported own-track curve/gradient change points; supported Structure, Signal, and Station placements; linked `Repeater.Begin`/`Begin0`/`End` segments; and the Section, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance statements listed below. The New Map Element wizard can insert the supported map/event/effect statements listed below, while preserving the shared distance-expression and source-writeback workflow. It supports live X/Y/Z Structure, Signal, and Repeater-Begin placement edits in the 3D scene. It is not yet a full map editor: PreTrain/other-train editing, Curve/Gradient method conversion or insertion, and automatic radius-based speed-limit calculation are still planned. Resource/list definition rows remain inline-table-only. Limited explicit conversion is available for `Structure.Put0`, `Repeater.Begin0`, and short-form `Signal.Put` when confirmed by the user.

## Development Status (TODO List)

### Map Loading and Parsing

- [x] Load `BveTs Map 2.0+` map files.
- [x] Handle UTF-8, UTF-8 with BOM, UTF-16LE, UTF-16BE, CP932/Shift_JIS, and related text encodings.
- [x] Support `Include` references to other map files.
- [x] Support `$variable = expression;`, the predefined `distance` variable, and basic math functions.
- [x] Support `#` and `//` comments.
- [x] Load maps asynchronously and show logs, warnings, and errors in the console window.
- [x] Expose source anchors and stable edit metadata for editable map/list statements through the versioned typed map snapshot.
- [x] Apply supported updates/deletions to an in-memory working copy and save them to source map/include/list files, preserving include structure, distance semantics, original encodings, and line endings where possible.
- [x] Block writeback when an edited value cannot be represented in the source encoding; there is currently no Save-as-UTF-8 fallback.
- [x] Reject ambiguous maps with multiple same-kind unkeyed resource-list `Load` statements or multiple case-insensitively matching `Train[].Enable` declarations.
- [x] Insert supported map placement and event/effect statements through the New Map Element wizard; resource/list definition rows remain inline-table-only.

### Own-Track and Other-Track Geometry

- [x] Parse and calculate own-track curves.
- [x] Parse and calculate own-track gradients, including their horizontal projection in plan geometry.
- [x] Support legacy syntax.
- [x] Parse the supported legacy own-track statements `Legacy.Turn`, `Legacy.Curve`, and `Legacy.Pitch`.
- [x] Parse and calculate parts of other-track position data, lateral/vertical interpolation, gauge, center offset, and cant.
- [x] Parse other-track `Track.Position`, `Track.X/Y.Interpolate`, `Track.Gauge`, and `Track.Cant.*` statements.
- [x] Support control-point range and interval settings, with geometry regeneration.
- [x] Load and display speed-limit sections.
- [x] Edit or delete existing own-track curve change points, including paired `Curve.BeginTransition` statements where applicable; insertion and method conversion are not supported.
- [x] Edit or delete existing own-track gradient change points, including paired `Gradient.BeginTransition` statements where applicable; insertion and method conversion are not supported.
- [x] Edit or delete supported existing other-track change statements from their edit-mode 2D/3D markers. Track key, method, and argument count remain read-only; insertion, dragging, gizmos, and method conversion are not supported.

### 2D Plan View and Charts

- [x] Display the own-track plan view.
- [x] Display enabled other tracks, with configurable visible range and color.
- [x] Display station positions, names, and mileage.
- [x] Display speed-limit markers.
- [x] Display curve-radius sections and transition-curve sections.
- [x] Display the profile/elevation chart.
- [x] Display the curve-radius chart.
- [x] Support panning, mouse-wheel zooming, rotation, and double-click fit-to-view in the plan view.
- [x] Support fixed grid, movable grid, and no-grid modes.
- [x] Support measurement mode, showing mileage, elevation, gradient, curve radius, and speed limit.
- [x] Support jumping to stations and numeric map distances.
- [x] Import a background image and adjust its position, size, rotation, and brightness.
- [x] Align a background image using two station positions.
- [x] Structure and repeater placement markers on the plan view.
- [x] Display signal position markers on the plan view.
- [x] Display `Section.Begin`/`Section.BeginNew` markers with their signal-index parameter labels on the plan view.
- [x] Display beacon position markers on the plan view.
- [x] Display PreTrain pass-point markers on the plan view.
- [x] Display other-train paths and stop-point markers on the plan view.
- [x] Display track-irregularity and adhesion change-point markers on the plan view.
- [x] Display map sound, fixed sound source, rolling-noise, flange-noise, and joint-noise markers on the plan view.
- [x] Display background change-point markers on the plan view.
- [x] Display cab-illuminance change-point markers on the plan view.
- [x] Display fog-effect change-point markers on the plan view.
- [x] Display draw-distance change-point markers on the plan view.
- [x] Open Properties/Edit or delete paired curve/gradient change points from the plan, profile, curve-radius, and 3D scene markers.
- [ ] Split 2D canvas internals into view state, marker cache, hit testing/context menus, background image handling, and drawing primitives without changing current behavior.

### Map Data Tables

- [x] Load the station list specified by `Station.Load`.
- [x] Display `Station.Put` position rows separately from `Station.Load` definition rows.
- [x] Display the other-track list, with controls for visibility, range, and color.
- [x] Display other-train definitions and stop-point lists, including each group's unique read-only `Train.Enable` time, with plan-path visibility and stop-point location.
- [x] Display map Structure placement tables for `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween`.
- [x] Load and display Structure model lists referenced by `Structure.Load` (`.txt` or `.csv`).
- [x] Display linked `Repeater.Begin`, `Repeater.Begin0`, and `Repeater.End` segments, with Begin/End/change boundaries merged for readability.
- [x] Display dynamic-column `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit` tables with explicit `null` arguments and source files; edit or delete existing rows through the source-backed inspector, including add/remove of the variable-length parameters.
- [x] Display a read-only variable-assignment list grouped by case-insensitive name, preserving parse order, original expressions, and source files.
- [x] Show evaluated `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load` arguments with their raw expressions and resolved paths above the corresponding list tables.
- [x] Provide shared find and unused-entry search panels for Structure models, signal aspects, and sound lists.
- [x] Edit, clear, reorder, or delete existing Structure model-list keys and file paths through the source-backed inline table editor; selecting a file writes a relative path where possible.
- [x] Edit or delete existing `Station.Put` rows, including distance, `stationKey`, door side, and stop margins.
- [x] Edit, clear, reorder, or delete existing station-definition rows loaded through `Station.Load`; adding station rows is not supported.
- [x] Display `Signal Aspect List`, `Map Signal List`, and `Beacon List`.
- [x] Display `Speed Limit Point List`, `Track Irregularity List`, `Adhesion Change Point List`, rolling-noise, flange-noise, and joint-noise tables.
- [x] Display `Background Change Point List`, `Cab Illuminance Change Point List`, `Fog Change Point List`, and `Draw Distance Change Point List`.
- [x] Edit, clear, reorder, or delete existing `Signal.Load` aspect definitions and their optional glare rows through the source-backed inline table editor; adding rows or structure-key columns is not supported.
- [x] Edit or delete existing `Beacon.Put` rows through the source-backed property inspector.
- [x] Provide a source-backed `Properties/Edit` inspector for supported Structure/Signal/Station/Repeater placements and Section, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance rows; expose it from applicable tables and 2D/3D markers, with live X/Y/Z gizmos for editable Structure, Signal, and Repeater Begin placements.
- [x] Open Properties/Edit for Structure and Signal placements from their 2D plan markers.
- [ ] Add direct 2D manipulation for Structure/Signal placements and extend the property inspector to remaining unsupported Map Info rows.

### 3D Canvas

- [x] 3D preview for Structure models.
- [x] Load model geometry, materials, and diffuse textures through `model_loader.dll`/Assimp.
- [x] Rotate and zoom the Structure model preview.
- [x] 3D scene preview canvas for track paths, Structure/Repeater instances, signals, map-element markers, background changes, and interpolated BVE fog effects.
- [x] Jump the 3D scene camera from station selections and numeric distance jumps, and show the current 3D position on the plan view.
- [x] Locate Structure, Repeater, signal, and supported map-marker table rows in the 3D scene preview, and locate picked scene objects or markers back in their tables.
- [ ] 3D scene quality settings for render scale, MSAA, texture filtering, and outline quality.
- [x] Display the current curve radius/cant, gradient, active speed limit, section-selected signal speeds, and distance to the next station in the 3D scene route overlay.
- [ ] Extend the 3D route overlay with previous-station information and unsupported interpolation cases.
- [x] Edit `Structure.Put`, `Signal.Put`, and `Repeater.Begin` positions along X/Y/Z with live 3D gizmos, including explicit `Put0`/`Begin0` conversion and configurable gizmo size.
- [ ] Add 3D gizmo editing for Structure rotation and other placement fields.
- [x] Edit linked Repeater segments in the inspector, including Begin navigation, End/change boundaries, and linked deletion choices.

### Environmental Effects

- [x] Display `Sound File List`, `3D Sound File List`, `Map Sound List`, and `Map 3D Sound List`.
- [x] Edit, clear, reorder, or delete existing `Sound.Load` and `Sound3D.Load` file-list rows through source-backed inline tables; selecting a file writes a relative path where possible.
- [x] Edit or delete existing `Sound.Play`/`Sound3D.Put` placements and rolling/flange/joint-noise events; station definition announcement sound keys are editable, but Sound/Sound3D file-list row insertion and audio playback remain unsupported.
- [x] Edit or delete existing cab-illuminance setting positions.
- [x] Edit or delete existing fog effects.

### User Interface and Utilities

- [x] Dear ImGui docking-based multi-window layout.
- [x] UI language switching between Simplified Chinese, English, and Japanese.
- [x] Settings for font size, UI component size, station marker size, 2D line widths, theme color, 3D scene draw distance/fog/map-draw-distance, camera speed, gizmo size, and scene-instance performance warnings.
- [x] Recent-map history.
- [x] Save background-image parameters with recent-map entries in `settings/history.ini`.
- [x] Save settings to `settings/settings.ini` under the executable directory.
- [x] Include-file structure diagram and read-only source text preview using the active in-memory working copy.
- [x] Edit mode with separate Apply-to-preview, Save-to-disk, global Revert of all pending changes, and Reload-from-disk behavior plus unsaved-change prompts.
- [x] Export own-track and other-track geometry to CSV.
- [ ] Element preset groups stored as ordinary BVE map/list statements through `element_presets.json`.
- [ ] Route release export that expands includes, optionally constantizes distance/variable expressions, copies only used resources, writes a report, and protects development route directories from overwrite.

### Current BVE Map Syntax Support

- Preview: the syntax actually feeds track geometry, tables, markers, or the 3D scene.
- Basic editing: existing statements can be changed and written back through the property inspector; this does not imply support for creating new statements.
- Graphical editing: elements can be dragged or manipulated directly on the 2D/3D canvas; opening the property window from a context menu does not count.
- √ = fully supported; △ = partially or indirectly supported; ✕ = currently unsupported; - = support is not planned or not applicable.

The New Map Element wizard inserts the supported map placement and event/effect forms represented in this table. It does not insert `Load`/resource-definition rows, and its numeric target distance uses the existing source-expression and distance-boundary workflow; existing `$` variable expressions are preserved or adjusted when safe.

| Map syntax                                    | Preview | Basic editing | Graphical editing | Current behavior                                                                                                         |
| --------------------------------------------- | :-----: | :-----------: | :---------------: | ------------------------------------------------------------------------------------------------------------------------ |
| File header, version, and encoding            |    △    |       ✕       |         -         | Supported encodings can be loaded, but encoding coverage is not complete                                                |
| Comments, assignments, calls, arrays, and keys |    √    |       ✕       |         -         | Used as parsing foundations; there is no general-purpose source editor                                                   |
| Variables and argument variables              |    √    |       ✕       |         -         | Participate in expression evaluation and appear in a read-only assignment/source list grouped case-insensitively       |
| Arithmetic operators (`+`, `-`, `*`, `/`, `%`) |    √    |       ✕       |         -         | Numeric arithmetic and `+` string concatenation are supported; comparison and logical operators are not supported          |
| Mathematical functions                        |    √    |       ✕       |         -         | Supported functions are `rand`, `abs`, `sin`, `cos`, `atan2`, `sqrt`, `exp`, `log`, `floor`, `ceil`, and `pow`           |
| Distance declarations and expressions         |    √    |       △       |         ✕         | Distance can be changed for the supported curve/gradient, placement, Repeater, speed-limit, Section, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance targets listed below |
| `include` and distance-offset `include`        |    √    |       △       |         ✕         | Include files can be loaded and supported elements in them can be written back, but Include paths are not editable       |
| `Curve.*`                                     |    √    |       △       |         ✕         | Existing change points support method-preserving distance/radius/cant edits and deletion; a paired `BeginTransition` is edited from the same inspector and deleted with its consuming Begin/End; no insertion or method conversion |
| `Gradient.*`                                  |    √    |       △       |         ✕         | Existing change points support method-preserving distance/gradient edits and deletion; a paired `BeginTransition` is edited from the same inspector and deleted with its consuming Begin/End; no insertion or method conversion |
| `Legacy.Turn` / `Legacy.Pitch`                |    √    |       ✕       |         ✕         | Feed legacy own-track direction/gradient state; source-backed editing is not available                                  |
| `Legacy.Curve`                                |    √    |       △       |         ✕         | Feeds own-track curve state and is exposed as an existing curve row; no insertion or method conversion                 |
| `Track['key'].Position`, `X/Y.Interpolate`    |    √    |       △       |         ✕         | Generates other-track geometry; existing distance and method-appropriate numeric parameters can be edited, but track key, method, and parameter count are read-only |
| `Track['key'].Gauge` / `Cant.*`               |    √    |       △       |         ✕         | Feeds track geometry and cant data; existing distance and method-appropriate numeric parameters can be edited, but track key, method, and parameter count are read-only |
| `Structure.Load`                              |    √    |       △       |         -         | Existing loaded-list keys/paths support inline edits, clearing, reordering, and deletion; the Load path and new rows are not editable |
| `Structure.Put`                               |    √    |       √       |         △         | Property fields can be written back; 3D supports only X/Y/Z translation, not direct rotation, distance, track, tilt, or span manipulation |
| `Structure.Put0`                              |    √    |       √       |         △         | Basic fields are editable; the X/Y/Z gizmo appears only after confirming conversion to `Structure.Put`                  |
| `Structure.PutBetween`                        |    √    |       √       |         ✕         | Editable in the property inspector, but has no 2D/3D gizmo                                                              |
| `Repeater.Begin` / `Begin0` / `End`           |    √    |       △       |         △         | Linked Begin fields, End distance, and linked deletion/trim actions are supported; `Begin0` can be explicitly converted to `Begin` for coordinate editing, and a trim action can convert a Begin to End |
| `Background.Change`                           |    √    |       √       |         ✕         | Distance and Structure key can be edited and the entry can be deleted; it feeds background data and the scene preview   |
| `Station.Load`                                |    √    |       △       |         -         | Existing referenced station-definition rows can be edited, cleared, reordered, or deleted; the Load path and new rows are not editable |
| `Station.Put`                                 |    √    |       √       |         ✕         | Distance, `stationKey`, door side, and stop margins can be edited, and the entry can be deleted                           |
| `Section.Begin` / `Section.BeginNew`          |    √    |       √       |         ✕         | Distance and the variable-length signal-index parameters can be edited (parameters can be added or removed), and the statement can be deleted; green `S` markers labeled with their signal-index arguments appear in the 2D plan and 3D scene |
| `Section.SetSpeedLimit` / `Signal.SpeedLimit` |    √    |       √       |         ✕         | Distance and the variable-length speed parameters can be edited (parameters can be added or removed), and the statement can be deleted; the latest row at the current position feeds the 3D `Signal:` summary |
| `Signal.Load`                                 |    √    |       √       |         -         | Existing signal-aspect rows and optional glare rows support inline edits, clearing, reordering, and deletion; adding rows or structure-key columns is not supported, and the GUI exposes at most 509 structure-key columns |
| `Signal.Put`                                  |    √    |       √       |         △         | All placement fields are editable and the statement can be deleted; 3D supports X/Y/Z translation. Editing extended fields on the short form requires confirmation before conversion to the full form |
| `Beacon.Put`                                  |    √    |       √       |         ✕         | Distance, type, section, and send data can be edited, and the entry can be deleted                                       |
| `SpeedLimit.Begin` / `SpeedLimit.End`         |    √    |       √       |         ✕         | Begin distance/speed and End distance can be edited; the New Map Element wizard can add either independent point; no type conversion |
| `PreTrain.Pass`                               |    √    |       ✕       |         ✕         | Feeds the list and map markers                                                                                           |
| `Light.Ambient/Diffuse/Direction`             |    -    |       -       |         -         | Lighting syntax is not supported                                                                                        |
| `Fog.Interpolate` / `Fog.Set`                 |    √    |       √       |         ✕         | Distance and method-appropriate density/color fields can be edited and the entry deleted; 3D preview applies interpolated exponential fog |
| `DrawDistance.Change`                         |    √    |       √       |         ✕         | Distance/value can be edited and the entry deleted; it can optionally control scene draw distance                       |
| `CabIlluminance.Interpolate` / `Set`          |    √    |       √       |         ✕         | Distance/value can be edited and the entry deleted; cab brightness is not simulated                                     |
| `Irregularity.Change`                         |    √    |       √       |         ✕         | Distance and 6 parameters can be edited in the Properties inspector and the entry can be deleted; vehicle vibration is not simulated |
| `Adhesion.Change`                             |    √    |       √       |         ✕         | Distance and the 1- or 3-parameter adhesion values can be edited and the entry deleted; effects are not simulated       |
| `Sound.Load` / `Sound.Play`                   |    √    |       △       |         ✕         | Existing Sound.Load rows support inline list editing; Sound.Play distance/key support Properties/Edit and deletion, but audio is not played |
| `Sound3D.Load` / `Sound3D.Put`                |    √    |       △       |         ✕         | Existing Sound3D.Load rows support inline list editing; Sound3D.Put distance/key/X/Y support Properties/Edit and deletion, but audio is not played |
| `RollingNoise.Change`                         |    √    |       √       |         ✕         | Distance/index can be edited and the entry deleted                                                                       |
| `FlangeNoise.Change`                          |    √    |       √       |         ✕         | Distance/index can be edited and the entry deleted                                                                       |
| `JointNoise.Play`                             |    √    |       √       |         ✕         | Distance/index can be edited and the entry deleted                                                                       |
| `Train.Add` / `Train.Load`                    |    △    |       ✕       |         ✕         | Other-train definitions can be displayed, but external train files are only partially modeled                           |
| `Train.Enable`                                |    √    |       ✕       |         ✕         | Its unique enable time is shown read-only above the matching other-train stop table                                      |
| `Train.Stop`                                  |    √    |       ✕       |         ✕         | Generates other-train stop tables, paths, and map markers                                                               |

## Installation and Startup

This repository does not currently provide a standalone installer or prebuilt release package. The recommended workflow is to build from source and run the generated executable.

After building, run `build_release\komapedit.exe`. The executable stays at the
top level, while `maploader.dll`, `model_loader.dll`, and copied Assimp/runtime
dependencies are loaded from `build_release\bin`.

On startup, the application creates the `settings` directory when necessary and
creates or reads the following files there:

- `settings/imgui.ini`: stores UI window positions and related ImGui layout data.
- `settings/settings.ini`: stores UI language, font/component/station-marker sizes, 2D line widths, theme color, edit-mode warning state, and 3D canvas options such as fog, draw distance, gizmo size, camera speed, and performance warnings.
- `settings/history.ini`: stores recent maps and background-image alignment parameters.

The build and distribution-cleanup scripts migrate legacy root-level INI files
into `settings`. If both legacy and new copies exist, the script stops instead
of overwriting either file.

## Usage

1. Use `File -> Open...` or the `Open` button on the toolbar to select a `.txt` or `.csv` map file.
2. After the map loads, the main window shows the plan view, profile chart, and curve-radius chart.
3. In the plan view:
   - Drag with the left mouse button to pan.
   - Use the mouse wheel to zoom.
   - Hold `Shift` while using the mouse wheel to rotate, or drag with the right mouse button / `Ctrl + left mouse button`.
   - Double-click the plan view to fit the map to the current viewport.
4. Use `Station Jump` or `Jump to distance(m)` on the toolbar to jump to a station or numeric map distance.
5. Use the `2D View` menu to show or hide the 2D view window, profile chart, curve-radius chart, gradient overlays, profile other-track display, and background-image controls. With Edit enabled, right-click paired curve/gradient change-point markers in the plan, profile, or curve-radius chart to open `Properties/Edit` or delete the source statements.
6. Use the `Auxiliary Info` menu to toggle plan marker groups for stations, track geometry, signals, sounds, effects, and 3D scene helpers. Other-train paths are controlled from the `Other Train List` in `Map Info`. `Section Markers` is off by default and controls the green `S` markers and their signal-index parameter labels in both 2D and 3D. Under `Auxiliary Info -> Other`, open `File Structure Diagram` to inspect the entry map and its nested Include files; right-click a source-file node to open its read-only text preview. Signal markers are controlled from the `Map Signal List` row `Show` checkboxes in `Map Info`.
7. Switch `Mode` to `Measure`, then move near the track or double-click to view mileage, elevation, gradient, curve radius, and speed limit.
8. Use the `Map Info` menu to open the data tables for stations, tracks, other trains, Structures, repeaters, signals, beacons, sounds, speed-limit points, irregularity/adhesion data, backgrounds, cab-illuminance, fog, and draw distance. Rows with plan positions can be located on the plan; model and sound file rows expose linked files.
   - `Signal Aspect List`: view and find signal aspect definitions; with Edit enabled, edit, reorder, clear, or delete existing aspect rows.
   - `Map Signal List`: view signal positions and use the row `Show` checkboxes to toggle markers on the plan; with Edit enabled, open `Properties/Edit` or delete an existing `Signal.Put` row.
   - `Beacon List`: view and locate beacon positions; with Edit enabled, open `Properties/Edit` or delete existing `Beacon.Put` rows.
   - `Sound File List` and `3D Sound File List`: view the loaded sound file entries, find matching keys, find unused entries, and open linked files. With Edit enabled, existing keys, paths, and buffer counts can be edited, reordered, cleared, or deleted; `Select File` writes a relative path where possible.
   - `Map Sound List`, `Map 3D Sound List`, `Rolling Noise Change Point List`, `Flange Noise Change Point List`, and `Joint Noise Play Point List`: view and locate sound playback/change positions; with Edit enabled, open `Properties/Edit` or delete existing events.
    - `Speed Limit Point List`, `Track Irregularity List`, `Adhesion Change Point List`, `Background Change Point List`, `Cab Illuminance Change Point List`, `Fog Change Point List`, and `Draw Distance Change Point List`: view and locate the corresponding change points; with Edit enabled, open `Properties/Edit` or delete supported rows. Speed-limit Begin and End points are independent; Begin exposes distance/speed while End exposes distance only.
   - `Other Tracks`: toggle other-track display and adjust visible range and color.
   - `Other Train List`: view other-train definitions and stop points, the unique read-only `Train.Enable` time for each stop group, toggle path visibility, and locate stop points on the plan.
   - `Section List`: view `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit` in separate dynamic-column tables, including explicit `null` arguments and source files. With Edit enabled, right-click a distance cell to open `Properties/Edit` or delete the row; the inspector edits distance and the variable-length parameters, with buttons to add or remove parameters.
   - `Variable List`: view every assignment grouped by case-insensitive variable name; hover a value for the original expression and use the source-file context menu to open its directory.
   - A map load is rejected when the entry map and its Includes contain more than one unkeyed Load of the same list type, or more than one case-insensitively matching `Train[].Enable`.
    - `Station List`: view `Station.Put` position rows separately from `Station.Load` definitions. With Edit enabled, position rows support `Properties/Edit` and deletion, while existing definition rows can be edited, reordered, cleared, or deleted.
   - `Map Structure List`: view `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween` entries from the map, and locate rendered objects in the 3D scene preview when it is loaded.
   - `Structure Model List`: view the structure keys and model files from the `Structure.Load` structure list. The Station, Structure Model, Signal Aspect, Sound File, and 3D Sound File lists show their original evaluated Load path at the top, with the raw expression/resolved path on hover and an Explorer context menu. Right-click a structure key and choose `Preview Model` to open the 3D model preview; with Edit enabled, existing keys and paths can be edited, reordered, cleared, or deleted.
    - `Repeater List`: inspect linked `Repeater.Begin`/`Begin0`/`End` segments, edit them through `Properties/Edit`, and locate generated repeaters in the 3D scene preview when it is loaded.
          - Turn on `Enable Edit` to use `Properties/Edit` on supported placement, Repeater, Section, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance rows from their tables or applicable 2D/3D markers. Use `New Map Element` to add the supported map/event/effect statement forms; its numeric target distance is resolved through the same source-expression, boundary, and environment workflow as `Properties/Edit`. Existing definition/resource rows from `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load` are edited in their respective inline tables, and list editors do not create new resource rows. Apply each inline-table draft before saving; otherwise `Save` is blocked. `Apply` updates the in-memory preview; the toolbar `Save` or `Ctrl+S` writes pending changes to source files, `Revert` discards all pending in-memory changes, and `Reload` reads the map from disk again.
     The first enablement asks for confirmation because editing is an unstable, experimental feature that may make destructive changes. Back up map files or manage them with version control such as Git; selecting `Don't show again` and confirming suppresses later warnings.
9. Use `2D View -> Background Image` to import a background image. You can adjust its position, size, rotation, and brightness manually, or align it using two stations.
10. Use `3D View -> Structure Model Preview` to show or hide the Structure model preview window. In the preview, drag with the left mouse button to rotate the model and use the mouse wheel to zoom.
11. Use `3D View -> 3D Scene Preview` to show the scene preview window, then click `Start 3D Scene Preview`. The scene preview can be reloaded or closed from that window; station and distance jumps also move the scene camera when a scene is loaded. The overlay shows current curve/cant, gradient, active `SpeedLimit.Begin`/`End` state, the signal speeds selected by the active `Section.Begin` indices from the current `Section.SetSpeedLimit`/`Signal.SpeedLimit` definition, and next-station information. `Options -> 3D Canvas Settings -> Fog effect` immediately toggles route fog in the scene preview and is enabled by default; the same settings also control map-driven draw distance, camera speed, and instance-performance warnings. In select mode, scene objects and supported map-element markers can be located back in their matching tables. With edit mode enabled, supported other-track changes appear as track-colored, selectable markers in the 2D plan and as track-colored signs in an already-started 3D scene; right-click them for `Properties/Edit` or deletion. Other supported scene markers use the same context path, while `Structure.Put`, `Signal.Put`, and `Repeater.Begin` coordinates can additionally be dragged with the X/Y/Z gizmo.
12. Use `File -> Export CSV...` to choose an output folder and export own-track and other-track geometry CSV files.
13. Press `F5` or use `File -> Reload` to reload the current map.

## Project Layout

```text
komapedit/
├─ CMakeLists.txt                  # CMake build configuration
├─ README.md                       # Project documentation
├─ README_zhcn.md                  # Simplified Chinese project documentation
├─ LICENSE                         # Apache License 2.0
├─ NOTICE                          # Project copyright and Apache attribution notices
├─ THIRD_PARTY_NOTICES.md          # Third-party library and reference-project notices
├─ build_dev.bat                   # Debug build script
├─ build_release.bat               # Release build script
├─ clear_build_release_dist.bat    # Cleans Release output while preserving bin, settings, and notices
├─ get_3rd_party_packages.bat      # Fetches ImGui and ImPlot
├─ install_Assimp.bat              # Helper for installing Assimp with vcpkg
├─ komapedit.rc                    # Windows application resource script
├─ icons/                           # Application icons and README title image
├─ include/
│  ├─ canvas3D.h                   # 3D preview canvas interface
│  ├─ map_marker_visuals.h          # Shared 2D/3D map-marker visual recipes
│  ├─ maploader.h                  # maploader C ABI
│  ├─ maploader_snapshot.h         # Fixed-width typed snapshot/edit ABI structures
│  ├─ model_loader.h               # model_loader C ABI
│  ├─ multilanguage.h              # UI localization strings
│  ├─ own_track_transition_linkage.h # Shared Curve/Gradient BeginTransition pairing rules
│  ├─ repeater_linkage.h            # Shared Repeater Begin/End segment pairing
│  └─ resource.h                   # Windows resource ID declarations
├─ src/
│  ├─ main_window/
│  │  ├─ gui_kme.cpp               # Main window, Win32/DirectX 11 setup, app loop
│  │  ├─ app_settings.cpp/.h       # Runtime settings, history, and UI style helpers
│  │  ├─ runtime_paths.cpp/.h       # Executable, DLL, and settings directory paths
│  │  ├─ maploader_runtime.cpp      # Cached runtime dispatch for bin/maploader.dll
│  │  ├─ map_marker_visuals.cpp     # Shared 2D/3D map-marker visuals
│  │  ├─ file_structure_diagram.cpp # Include-file structure diagram and source-file actions
│  │  ├─ text_preview.cpp            # Read-only source preview and distance-boundary selection
│  │  ├─ debug_headless.cpp/.h     # Debug-only headless validation entry points
│  │  ├─ touch_input.cpp/.h         # Win32 touch gesture translation
│  │  └─ kme.h                     # App declaration and shared GUI state
│  ├─ table/
│  │  ├─ datatable.cpp             # Data table columns, cache, and table windows
│  │  └─ table_navigation.cpp      # Table-to-plan/3D marker navigation and visibility state
│  ├─ canvas2d/
│  │  ├─ canvas2D.cpp              # 2D plan canvas, markers, measurement, and background image
│  │  └─ profile_plots.cpp         # Profile and curve-radius chart rendering
│  ├─ canvas3d/
│  │  └─ canvas3D.cpp              # DirectX 11 model/scene preview and scene-marker rendering
│  ├─ maploader/
│  │  ├─ maploader.cpp             # Public C ABI entry points and map handle lifecycle
│  │  ├─ maploader_internal.h      # Shared maploader state, row records, source anchors, and helpers
│  │  ├─ maploader_core.cpp        # Common parsing/value/source-span utilities and MapContext helpers
│  │  ├─ maploader_parser.cpp      # BVE Map/list parsing, Include handling, variables, and source anchors
│  │  ├─ maploader_geometry.cpp    # Own/other-track geometry, relocation, curves, and scene control points
│  │  ├─ maploader_identity.cpp    # Stable edit identities and deterministic hashing
│  │  ├─ maploader_snapshot.cpp    # Map/scene snapshot construction and revision invalidation
│  │  ├─ maploader_semantic.cpp    # Typed semantic edit validation and fingerprints
│  │  ├─ maploader_edits.cpp       # Edit dry-run, in-memory apply, source patching, and commit/writeback
│  │  ├─ tests/                     # Typed snapshot/edit C ABI contract tests
│  │  ├─ text_decoder.cpp/.h       # File reading, UTF-8 paths, and text decoding
│  │  ├─ diagnostics.cpp/.h        # Loader logs and last-error state
│  │  └─ c_api.cpp/.h              # C ABI allocation helpers
│  └─ model_loader/
│     └─ model_loader.cpp          # Assimp-based Structure model loading
├─ tests/                           # Maploader diagnostic-test fixtures
├─ third_party/
│  ├─ imgui/                       # Dear ImGui, docking branch
│  └─ implot/                      # ImPlot
├─ build/                          # Debug: komapedit.exe, bin/ DLLs, settings/ INIs
└─ build_release/                  # Release: komapedit.exe, bin/ DLLs, settings/ INIs
```

## Building From Source

### Requirements

- Windows
- [CMake](https://cmake.org/) 3.20 or newer
- [Ninja](https://github.com/ninja-build/ninja)
- A C++17-capable compiler, such as MSVC or [MinGW](https://www.mingw-w64.org/)
- Windows SDK / DirectX 11 / WIC development libraries
- Git, used to fetch third-party dependencies
- Assimp, discoverable by CMake as `assimp::assimp`

CMake 3.21 or newer is recommended when Assimp is not supplied through vcpkg or
an explicit prefix, because the generic runtime-DLL copy fallback requires it.

### Fetch Third-Party Dependencies: `get_3rd_party_packages.bat`

This script clones the following two projects. Make sure Git is installed first.

- `third_party/imgui` -> the `docking` branch of `ocornut/imgui`
- `third_party/implot` -> `epezent/implot`

The cloned third-party source trees will be ignored by Git. Their upstream license
files remain under `third_party/`; distribution notices are summarized in
`THIRD_PARTY_NOTICES.md`.

### Install Assimp: `install_Assimp.bat`

Assimp is not vendored under `third_party/`. Install it separately before
configuring the project. The provided build scripts automatically use vcpkg when
`VCPKG_ROOT` is set; if `VCPKG_DEFAULT_TRIPLET` is not set, they default to
`x64-mingw-dynamic`.

`install_Assimp.bat` is a helper for installing `assimp:x64-mingw-dynamic` with
vcpkg. It is hard-coded for that MinGW triplet; MSVC users should install the
appropriate triplet, such as `assimp:x64-windows`, separately. Edit the script
before using it and fill in the path to your local vcpkg directory.

### Debug Build: `build_dev.bat`

The output directory is `build`, using the same top-level executable, `bin` DLL,
and `settings` INI layout described below for Release.

### Release Build: `build_release.bat`

The output directory is `build_release`. The main build products are:

- `komapedit.exe`
- `bin/maploader.dll`
- `bin/model_loader.dll`
- Assimp/runtime DLLs under `bin`, depending on the selected toolchain/package manager
- `settings/`, initially empty until settings are written
- `LICENSE`
- `NOTICE`
- `THIRD_PARTY_NOTICES.md`

To clean the Release output for distribution, run
`clear_build_release_dist.bat`. It keeps `komapedit.exe`, `bin` and its `.dll`
files, the `settings` directory and its existing contents, plus `LICENSE`,
`NOTICE`, and `THIRD_PARTY_NOTICES.md`.

The build scripts create the `settings` directory, but `settings.ini`,
`history.ini`, and `imgui.ini` are created or updated by the application while it
runs and exits.

### Debug Tests

After a Debug build, run the registered CTest checks:

```bat
ctest --test-dir build --output-on-failure
```

The diagnostics contract requires the map fixtures under `tests/` to be present.
That directory is currently excluded by the repository's ignore rules, so a clean
checkout must provide the fixtures separately before this test can pass.

## Appendix: CSV Data Formats

### Own-Track Geometry CSV (Export)

Exported with `File -> Export CSV...`. File name format:

```text
<output-folder-name>_owntrack.csv
```

Header:

```csv
#distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge
```

Field reference:

| Field | Description |
| --- | --- |
| distance | Absolute map distance, in meters |
| x | Calculated own-track plan X coordinate after gradient projection |
| y | Calculated own-track plan Y coordinate after gradient projection |
| z | Elevation |
| direction | Track direction angle, in radians |
| radius | Current curve radius |
| gradient | Current gradient, using BVE's per-mille convention |
| interpolate_func | Interpolation type: `0` means sinusoidal half-wave easing, `1` means linear easing |
| cant | Cant |
| center | Track center offset |
| gauge | Track gauge |

### Other-Track Geometry CSV (Export)

Each other track is exported as a separate CSV file. File name format:

```text
<output-folder-name>_<trackKey>.csv
```

Header:

```csv
#distance,x,y,z,interpolate_func,cant,center,gauge
```

Field reference:

| Field | Description |
| --- | --- |
| distance | Absolute map distance, in meters |
| x | Calculated other-track plan X coordinate derived from the projected own track |
| y | Calculated other-track plan Y coordinate derived from the projected own track |
| z | Other-track elevation |
| interpolate_func | Interpolation type: `0` means `sin`, `1` means `line` |
| cant | Cant |
| center | Track center offset |
| gauge | Track gauge |

Exported numeric values use fixed six-decimal formatting. CSV export currently includes track geometry only; it does not export stations, Structures, repeaters, signals, beacons, sound/noise events, track-irregularity or adhesion changes, background change points, cab-illuminance change points, fog, draw-distance changes, or 3D scene data.

## License and Third-Party Notices

komapedit is distributed under the Apache License, Version 2.0. See `LICENSE`
for the license text and `NOTICE` for project attribution notices.

This project is based on `kobushi-trackviewer` and is intended to help inspect
and edit BVE Trainsim map files.

Reference project:

| Project | Copyright | License |
| --- | --- | --- |
| [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) by konawasabi | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

Third-party libraries used by the GUI and model preview:

| Library | Use | Copyright | License |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | Docking GUI, Win32 backend, DirectX 11 backend, C++ std::string helper | Copyright (c) 2014-2026 Omar Cornut | MIT License |
| [ImPlot](https://github.com/epezent/implot) | 2D plotting widgets | Copyright (c) 2020-2024 Evan Pezent; Copyright (c) 2025-2026 Breno Cunha Queiroz | MIT License |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | Structure model import | Copyright (c) 2006-2026, assimp team | Modified BSD 3-Clause License |
| stb single-file libraries bundled with Dear ImGui | Font/text/rectangle-packing support used by Dear ImGui | Copyright (c) 2017 Sean Barrett | MIT License or Public Domain |

When distributing source or binaries built from this repository, include
`LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`. If the `third_party/`
source trees are distributed, keep their original license files and copyright
notices intact.

Project page: <https://github.com/NewSapporoNingyo/komapedit>

## Star History

<a href="https://www.star-history.com/?repos=NewSapporoNingyo%2Fkomapedit&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
 </picture>
</a>
