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

The bundled executable and `maploader.dll` use maploader API v2. `KvMapSnapshot` v2 carries all map, regular-geometry, source, and edit metadata; the independently invalidated `KvSceneGeometrySnapshot` v1 carries dense 3D own/other-track geometry. Typed edit batches and handle-owned typed reports cover dry-run, in-memory Apply, direct Apply, Save/commit, and target lookup. These views are process-memory only: Open and Reload always read the current route sources, and no route snapshot or geometry cache is written to disk.

At this stage, komapedit provides source-backed editing for existing list rows referenced by `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load`, plus `Structure.Put`/`Put0`/`PutBetween`, `Signal.Put`, `Station.Put`, and linked `Repeater.Begin`/`Begin0`/`End` segments. It supports live X/Y/Z Structure, Signal, and Repeater-Begin placement edits in the 3D scene. It is not yet a full map editor: curve/gradient, map sound/event, environmental-effect, and new-element editing are still planned.

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
- [ ] Add new-element insertion and extend source-backed editing beyond the currently supported existing list rows, Structure/Signal/Station placements, and linked Repeater rows.

### Own-Track and Other-Track Geometry

- [x] Parse and calculate own-track curves.
- [x] Parse and calculate own-track gradients.
- [x] Support legacy syntax.
- [x] Parse and calculate parts of other-track position data, lateral/vertical interpolation, gauge, center offset, and cant.
- [x] Support control-point range and interval settings, with geometry regeneration.
- [x] Load and display speed-limit sections.
- [ ] Own-track curve editing.
- [ ] Own-track gradient editing.

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
- [x] Display beacon position markers on the plan view.
- [x] Display PreTrain pass-point markers on the plan view.
- [x] Display other-train paths and stop-point markers on the plan view.
- [x] Display track-irregularity and adhesion change-point markers on the plan view.
- [x] Display map sound, fixed sound source, rolling-noise, flange-noise, and joint-noise markers on the plan view.
- [x] Display background change-point markers on the plan view.
- [x] Display cab-illuminance change-point markers on the plan view.
- [x] Display fog-effect change-point markers on the plan view.
- [x] Display draw-distance change-point markers on the plan view.
- [ ] Split 2D canvas internals into view state, marker cache, hit testing/context menus, background image handling, and drawing primitives without changing current behavior.

### Map Data Tables

- [x] Load the station list specified by `Station.Load`.
- [x] Display `Station.Put` position rows separately from `Station.Load` definition rows.
- [x] Display the other-track list, with controls for visibility, range, and color.
- [x] Display other-train definitions and stop-point lists, with plan-path visibility and stop-point location.
- [x] Display map Structure placement tables for `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween`.
- [x] Load and display Structure model lists referenced by `Structure.Load` (`.txt` or `.csv`).
- [x] Display linked `Repeater.Begin`, `Repeater.Begin0`, and `Repeater.End` segments, with Begin/End/change boundaries merged for readability.
- [x] Provide shared find and unused-entry search panels for Structure models, signal aspects, and sound lists.
- [x] Edit, clear, reorder, or delete existing Structure model-list keys and file paths through the source-backed inline table editor; selecting a file writes a relative path where possible.
- [x] Edit `Station.Put` distance and `stationKey` fields.
- [x] Edit, clear, reorder, or delete existing station-definition rows loaded through `Station.Load`; adding station rows is not supported.
- [x] Display `Signal Aspect List`, `Map Signal List`, and `Beacon List`.
- [x] Display `Track Irregularity List`, `Adhesion Change Point List`, rolling-noise, flange-noise, and joint-noise tables.
- [x] Display `Background Change Point List`, `Cab Illuminance Change Point List`, `Fog Change Point List`, and `Draw Distance Change Point List`.
- [x] Edit, clear, reorder, or delete existing `Signal.Load` aspect definitions through the source-backed inline table editor; adding rows or structure-key columns is not supported.
- [ ] Edit beacon lists.
- [x] Provide a source-backed `Properties/Edit` inspector for supported Structure, `Signal.Put`, `Station.Put`, and linked Repeater rows; a 3D station-position marker can open its matching `Station.Put` inspector, and editable Structure, Signal, and Repeater Begin placements have live X/Y/Z gizmos.
- [ ] Extend the property inspector to 2D markers and remaining Map Info rows.

### 3D Canvas

- [x] 3D preview for Structure models.
- [x] Load model geometry, materials, and diffuse textures through `model_loader.dll`/Assimp.
- [x] Rotate and zoom the Structure model preview.
- [x] 3D scene preview canvas for track paths, Structure/Repeater instances, signals, map-element markers, background changes, and interpolated BVE fog effects.
- [x] Jump the 3D scene camera from station selections and numeric distance jumps, and show the current 3D position on the plan view.
- [x] Locate Structure, Repeater, signal, and supported map-marker table rows in the 3D scene preview, and locate picked scene objects or markers back in their tables.
- [ ] 3D scene quality settings for render scale, MSAA, texture filtering, and outline quality.
- [x] Display the current curve radius/cant, gradient, and distance to the next station in the 3D scene route overlay.
- [ ] Extend the 3D route overlay with previous-station information and unsupported interpolation cases.
- [x] Edit `Structure.Put`, `Signal.Put`, and `Repeater.Begin` positions along X/Y/Z with live 3D gizmos, including explicit `Put0`/`Begin0` conversion and configurable gizmo size.
- [ ] Add 3D gizmo editing for Structure rotation and other placement fields.
- [x] Edit linked Repeater segments in the inspector, including Begin navigation, End/change boundaries, and linked deletion choices.

### Environmental Effects

- [x] Display `Sound File List`, `3D Sound File List`, `Map Sound List`, and `Map 3D Sound List`.
- [x] Edit, clear, reorder, or delete existing `Sound.Load` and `Sound3D.Load` file-list rows through source-backed inline tables; selecting a file writes a relative path where possible.
- [ ] Add Sound/Sound3D file-list rows or edit map sounds, rolling-noises, joint-noises, station announcement sound fields, and 3D sound placements.
- [ ] Edit cab-illuminance setting positions.
- [ ] Edit fog effects.

### User Interface and Utilities

- [x] Dear ImGui docking-based multi-window layout.
- [x] UI language switching between Simplified Chinese, English, and Japanese.
- [x] Settings for font size, UI component size, station marker size, theme color, 3D scene camera speed, and scene-instance performance warnings.
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

| Map syntax                                    | Preview | Basic editing | Graphical editing | Current behavior                                                                                                         |
| --------------------------------------------- | :-----: | :-----------: | :---------------: | ------------------------------------------------------------------------------------------------------------------------ |
| File header, version, and encoding            |    △    |       ✕       |         -         | Supported encodings can be loaded, but encoding coverage is not complete                                                |
| Comments, assignments, calls, arrays, and keys |    √    |       ✕       |         -         | Used as parsing foundations; there is no general-purpose source editor                                                   |
| Variables and argument variables              |    √    |       ✕       |         -         | Can participate in expression evaluation                                                                                |
| Arithmetic, comparison, and logical operators |    △    |       ✕       |         -         | Most expressions can be evaluated, but edge cases such as mixed strings and numbers are incomplete                      |
| Mathematical functions                        |    √    |       ✕       |         -         | Used to evaluate map expressions                                                                                        |
| Distance declarations and expressions         |    √    |       △       |         ✕         | Distance can be changed only for edit targets such as `Structure.Put/Put0/PutBetween`, `Signal.Put`, `Station.Put`, `Irregularity.Change`, and linked Repeater Begin/End |
| `include` and distance-offset `include`        |    √    |       △       |         ✕         | Include files can be loaded and supported elements in them can be written back, but Include paths are not editable       |
| `Curve.*`                                     |    √    |       ✕       |         ✕         | Participates in track-geometry generation                                                                               |
| `Gradient.*`                                  |    √    |       ✕       |         ✕         | Participates in track elevation and gradient generation                                                                 |
| `Track['key'].X/Y/Position`                   |    √    |       ✕       |         ✕         | Generates other-track geometry                                                                                          |
| `Track['key'].Cant.*`                         |    √    |       ✕       |         ✕         | Feeds track geometry and cant data                                                                                      |
| `Structure.Load`                              |    √    |       △       |         -         | Existing loaded-list keys/paths support inline edits, clearing, reordering, and deletion; the Load path and new rows are not editable |
| `Structure.Put`                               |    √    |       √       |         △         | Property fields can be written back; 3D supports only X/Y/Z translation, not direct rotation, distance, track, tilt, or span manipulation |
| `Structure.Put0`                              |    √    |       √       |         △         | Basic fields are editable; the X/Y/Z gizmo appears only after confirming conversion to `Structure.Put`                  |
| `Structure.PutBetween`                        |    √    |       √       |         ✕         | Editable in the property inspector, but has no 2D/3D gizmo                                                              |
| `Repeater.Begin` / `Begin0` / `End`           |    √    |       △       |         △         | Linked Begin fields, End distance, and linked deletion are supported; a Begin supports X/Y/Z motion in 3D after explicit `Begin0` conversion |
| `Background.Change`                           |    √    |       ✕       |         ✕         | Feeds background data and the scene preview                                                                             |
| `Station.Load`                                |    √    |       △       |         -         | Existing referenced station-definition rows can be edited, cleared, reordered, or deleted; the Load path and new rows are not editable |
| `Station.Put`                                 |    √    |       △       |         ✕         | Only distance and `stationKey` are editable; door side, stop margins, and other parameters are not                       |
| `Section.Begin` / `Section.BeginNew`          |    ✕    |       ✕       |         ✕         | Parsed into the typed map snapshot, but the current GUI does not display it                                               |
| `Section.SetSpeedLimit` / `Signal.SpeedLimit` |    ✕    |       ✕       |         ✕         | Parsed into the typed map snapshot, but the current GUI does not display it                                               |
| `Signal.Load`                                 |    √    |       √       |         -         | Existing signal-aspect rows support inline edits, clearing, reordering, and deletion; adding rows or structure-key columns is not supported |
| `Signal.Put`                                  |    √    |       √       |         △         | All placement fields are editable; 3D supports X/Y/Z translation. Editing extended fields on the short form requires confirmation before conversion to the full form |
| `Beacon.Put`                                  |    √    |       ✕       |         ✕         | Feeds the list and map markers                                                                                           |
| `SpeedLimit.Begin` / `SpeedLimit.End`         |    √    |       ✕       |         ✕         | Feeds the speed-limit model, list, and markers                                                                           |
| `PreTrain.Pass`                               |    √    |       ✕       |         ✕         | Feeds the list and map markers                                                                                           |
| `Light.Ambient/Diffuse/Direction`             |    -    |       -       |         -         | Lighting syntax is not supported                                                                                        |
| `Fog.Interpolate` / `Fog.Set`                 |    √    |       ✕       |         ✕         | Feeds tables/markers and linearly interpolated exponential fog in the 3D scene preview; fog editing is not supported     |
| `DrawDistance.Change`                         |    √    |       ✕       |         ✕         | Feeds its table, plan/scene markers, and optional scene draw-distance control                                           |
| `CabIlluminance.Set`                          |    √    |       ✕       |         ✕         | Feeds lists/markers, but cab brightness is not simulated                                                                 |
| `Irregularity.Change`                         |    √    |       √       |         ✕         | Distance and 6 parameters can be edited in the Properties inspector and the entry can be deleted; vehicle vibration is not simulated |
| `Adhesion.Change`                             |    √    |       ✕       |         ✕         | Feeds lists/markers, but vehicle adhesion effects are not simulated                                                      |
| `Sound.Load` / `Sound.Play`                   |    √    |       △       |         ✕         | Existing Sound.Load file-list rows support inline edits, clearing, reordering, deletion, and file selection; playback points stay read-only and audio is not played |
| `Sound3D.Load` / `Sound3D.Put`                |    √    |       △       |         ✕         | Existing Sound3D.Load file-list rows support inline edits, clearing, reordering, deletion, and file selection; spatial placements stay read-only and audio is not played |
| `RollingNoise.Change`                         |    √    |       ✕       |         ✕         | Feeds lists/markers                                                                                                      |
| `FlangeNoise.Change`                          |    √    |       ✕       |         ✕         | Feeds lists/markers                                                                                                      |
| `JointNoise.Play`                             |    √    |       ✕       |         ✕         | Feeds lists/markers                                                                                                      |
| `Train.Add` / `Train.Load`                    |    △    |       ✕       |         ✕         | Other-train definitions can be displayed, but external train files are only partially modeled                           |
| `Train.Enable`                                |    ✕    |       ✕       |         ✕         | Parsed into the typed map snapshot but not exposed in the current GUI                                                   |
| `Train.Stop`                                  |    √    |       ✕       |         ✕         | Generates other-train stop tables, paths, and map markers                                                               |

## Installation and Startup

This repository does not currently provide a standalone installer. The recommended workflow is to build from source and run the generated executable.

After building, run `build_release\komapedit.exe`. The executable stays at the
top level, while `maploader.dll`, `model_loader.dll`, and copied Assimp/runtime
dependencies are loaded from `build_release\bin`.

On startup, the application creates the `settings` directory when necessary and
creates or reads the following files there:

- `settings/imgui.ini`: stores UI window positions and related ImGui layout data.
- `settings/settings.ini`: stores settings such as UI language, font size, component size, station marker size, theme color, and 3D canvas options.
- `settings/history.ini`: stores recent maps and background-image alignment parameters.

The build and distribution-cleanup scripts migrate legacy root-level INI files
into `settings`. If both legacy and new copies exist, the script stops instead
of overwriting either file.

## Usage

1. Use `File -> Open...` or the `Open` button on the toolbar to select a `.txt` map file.
2. After the map loads, the main window shows the plan view, profile chart, and curve-radius chart.
3. In the plan view:
   - Drag with the left mouse button to pan.
   - Use the mouse wheel to zoom.
   - Hold `Shift` while using the mouse wheel to rotate, or drag with the right mouse button / `Ctrl + left mouse button`.
   - Double-click the plan view to fit the map to the current viewport.
4. Use `Station Jump` or `Jump to distance(m)` on the toolbar to jump to a station or numeric map distance.
5. Use the `2D View` menu to show or hide the 2D view window, profile chart, curve-radius chart, gradient overlays, profile other-track display, and background-image controls.
6. Use the `Auxiliary Info` menu to toggle plan marker groups for stations, track geometry, other-train paths, signals, sounds, effects, and 3D scene helpers. Under `Auxiliary Info -> Other`, open `File Structure Diagram` to inspect the entry map and its nested Include files; right-click a source-file node to open its read-only text preview. Signal markers are controlled from the `Map Signal List` row `Show` checkboxes in `Map Info`.
7. Switch `Mode` to `Measure`, then move near the track or double-click to view mileage, elevation, gradient, curve radius, and speed limit.
8. Use the `Map Info` menu to open the data tables for stations, tracks, other trains, Structures, repeaters, signals, beacons, sounds, irregularity/adhesion data, backgrounds, cab-illuminance, fog, and draw distance. Rows with plan positions can be located on the plan; model and sound file rows expose linked files.
   - `Signal Aspect List`: view and find signal aspect definitions; with Edit enabled, edit, reorder, clear, or delete existing aspect rows.
   - `Map Signal List`: view signal positions and use the row `Show` checkboxes to toggle markers on the plan.
   - `Beacon List`: view beacon positions.
   - `Sound File List` and `3D Sound File List`: view the loaded sound file entries, find matching keys, find unused entries, and open linked files. With Edit enabled, existing keys, paths, and buffer counts can be edited, reordered, cleared, or deleted; `Select File` writes a relative path where possible.
   - `Map Sound List`, `Map 3D Sound List`, `Rolling Noise Change Point List`, `Flange Noise Change Point List`, and `Joint Noise Play Point List`: view sound playback/change positions and locate them on the plan.
    - `Track Irregularity List`, `Adhesion Change Point List`, `Background Change Point List`, `Cab Illuminance Change Point List`, `Fog Change Point List`, and `Draw Distance Change Point List`: view the corresponding change-point tables.
   - `Other Tracks`: toggle other-track display and adjust visible range and color.
   - `Other Train List`: view other-train definitions and stop points, toggle path visibility, and locate stop points on the plan.
    - `Station List`: view `Station.Put` position rows separately from `Station.Load` definitions. With Edit enabled, existing definition rows can be edited, reordered, cleared, or deleted.
   - `Map Structure List`: view `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween` entries from the map, and locate rendered objects in the 3D scene preview when it is loaded.
   - `Structure Model List`: view the structure keys and model files from the `Structure.Load` structure list. Right-click a structure key and choose `Preview Model` to open the 3D model preview; with Edit enabled, existing keys and paths can be edited, reordered, cleared, or deleted.
    - `Repeater List`: inspect linked `Repeater.Begin`/`Begin0`/`End` segments, edit them through `Properties/Edit`, and locate generated repeaters in the 3D scene preview when it is loaded.
    - Turn on `Enable Edit` to use `Properties/Edit` on supported `Station.Put`, `Signal.Put`, Structure placements, and linked Repeater rows. Existing definition/resource rows from `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load` are edited in their respective inline tables; no currently supported list editor creates a new row. `Apply` updates the in-memory preview; the toolbar `Save` writes pending changes to source files, `Revert` discards all pending in-memory changes, and `Reload` reads the map from disk again.
     The first enablement asks for confirmation because editing is an unstable, experimental feature that may make destructive changes. Back up map files or manage them with version control such as Git; selecting `Don't show again` and confirming suppresses later warnings.
9. Use `2D View -> Background Image` to import a background image. You can adjust its position, size, rotation, and brightness manually, or align it using two stations.
10. Use `3D View -> Structure Model Preview` to show or hide the Structure model preview window. In the preview, drag with the left mouse button to rotate the model and use the mouse wheel to zoom.
11. Use `3D View -> 3D Scene Preview` to show the scene preview window, then click `Start 3D Scene Preview`. The scene preview can be reloaded or closed from that window; station and distance jumps also move the scene camera when a scene is loaded. The overlay shows current curve/cant, gradient, and next-station information. `Options -> 3D Canvas Settings -> Fog effect` immediately toggles route fog in the scene preview and is enabled by default; the same settings also control map-driven draw distance, camera speed, and instance-performance warnings. In select mode, scene objects and supported map-element markers can be located back in their matching tables. With edit mode enabled, right-click a Structure, Signal, Repeater, or station-position marker and choose `Properties/Edit`; supported `Structure.Put`, `Signal.Put`, and `Repeater.Begin` coordinates can be dragged with the X/Y/Z gizmo.
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
├─ include/
│  ├─ canvas3D.h                   # 3D preview canvas interface
│  ├─ map_marker_visuals.h          # Shared 2D/3D map-marker visual recipes
│  ├─ maploader.h                  # maploader C ABI
│  ├─ maploader_snapshot.h         # Fixed-width typed snapshot/edit ABI structures
│  ├─ model_loader.h               # model_loader C ABI
│  ├─ repeater_linkage.h            # Shared Repeater Begin/End segment pairing
│  └─ multilanguage.h              # UI localization strings
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
vcpkg. Edit the script before using it and fill in the path to your local vcpkg
directory.

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
| x | Calculated own-track plan X coordinate |
| y | Calculated own-track plan Y coordinate |
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
| x | Calculated other-track plan X coordinate |
| y | Calculated other-track plan Y coordinate |
| z | Other-track elevation |
| interpolate_func | Interpolation type: `0` means `sin`, `1` means `line` |
| cant | Cant |
| center | Track center offset |
| gauge | Track gauge |

Exported numeric values use fixed six-decimal formatting. CSV export currently includes track geometry only; it does not export stations, Structures, repeaters, signals, beacons, sounds, background change points, cab-illuminance change points, fog, or 3D scene data.

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
| [ImPlot](https://github.com/epezent/implot) | 2D plotting widgets | Copyright (c) 2020 Evan Pezent | MIT License |
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
