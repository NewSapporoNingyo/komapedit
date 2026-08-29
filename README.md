<p align="center">
    <img src="icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit is a lightweight Windows viewer and editor for BVE Trainsim map files. It loads route maps and their Include files, calculates own-track and other-track geometry, presents the route in 2D charts and a 3D scene, exposes map data in searchable tables, previews Structure models, and exports calculated track geometry to CSV.

Editing is source-backed but still experimental. Back up route files or keep them under version control before enabling Edit mode. komapedit is not yet a complete map editor: some BVE syntax is preview-only or unsupported, and some supported statements can be edited but not created or manipulated graphically. See [Current BVE Map Syntax Support](#current-bve-map-syntax-support) for the current boundaries and [TODO.md](TODO.md) for implementation progress.

## Documentation

- [简体中文README](docs/README_zhcn.md)
- [Development status (Todo List)](TODO.md)
- [Developer guide](docs/dev.md) ([简体中文版](docs/dev_zhcn.md))
- [AI-assisted development guide](docs/ai-dev.md) ([简体中文版](docs/ai-dev_zhcn.md))
- [Repository instructions for AI coding tools](AGENTS.md)
- [License](LICENSE), [project notice](NOTICE), and [third-party notices](THIRD_PARTY_NOTICES.md)

## Features

- Opens BVE Trainsim 2.0+ map files in UTF-8, UTF-16, CP932/Shift_JIS-related encodings, including nested `Include` files; missing or invalid included maps are skipped with a warning so remaining elements can load.
- Opens maps through BVE Scenario files (`BveTs Scenario 2.00`) by resolving the official `Route` entry relative to the scenario directory; multiple weighted candidates are chosen in a selection dialog. `Map Info List -> Other -> Scenario File` provides a read-only view of all eight official fields, preserving source-relative Route and Vehicle paths and their weights.
- Displays plan, elevation/profile, curve-radius, station, speed-limit, other-track, marker, and measurement information.
- Provides searchable tables for stations, tracks, Structures, repeaters, signals, beacons, sounds, trains, and environmental effects.
- Previews Structure models and a route scene in 3D.
- Supports source-backed Apply, Save, Revert, and Reload for the statement families listed below, plus selected live X/Y/Z placement edits in the 3D scene.
- With Edit enabled, the File Structure Diagram can import an existing submap or exclusively create a UTF-8 submap with the standard BVE header, then stage its Include in the selected physical source file.
- `File -> New...` opens a New File Wizard for a header-only `BveTs Map 2.02` map or Structure, Signal, Sound, Sound3D, and Station list files. Resource-list templates offer `Import File`, which selects an existing `.txt`/`.csv` and fills its editable name, directory, and suffix fields. The wizard always appends its selected `.txt` (default) or `.csv` suffix to the raw name, so `route.csv` with `.txt` becomes `route.csv.txt`; an existing regular target file is reused without being written, otherwise a header-only file is created. Selecting any loaded non-resource-list map source file stages its `include` or matching `*.Load` reference in the working copy; official BVE does not require a distance statement in the target, so distance-free blank maps are valid reference targets too. Normal Save writes that parent-map reference, while Revert leaves created files on disk. The Presets category is currently empty and the wizard does not add list rows or Scenario files.
- Resource lists that only carry their file header are valid starting points: the resource-list tables offer an `Add Row` button (and context-menu insertion) that creates the first row directly, without a comma placeholder line.
- Exports calculated own-track and other-track geometry to CSV.
- Offers Simplified Chinese, English, and Japanese UI languages.


## Current BVE Map Syntax Support

- Preview: the syntax actually feeds track geometry, tables, markers, or the 3D scene.
- Basic editing: existing statements can be changed and written back through the property inspector; this does not imply support for creating new statements.
- New element: the New Map Element wizard can insert a corresponding source statement.
- Graphical editing: elements can be dragged or manipulated directly on the 2D/3D canvas; opening the property window from a context menu does not count.
- √ = fully supported; △ = partially or indirectly supported; ✕ = currently unsupported; - = support is not planned or not applicable.

The statement names and `[legacy]` aliases below follow the [official BVE map file-format reference](https://bvets.net/jp/edit/formats/route/map.html). The project-specific `Legacy.*` compatibility statements are retained for completeness. The wizard does not insert `Load`/resource-definition rows; numeric target distances use the existing source-expression and distance-boundary workflow, preserving or safely adjusting existing `$` expressions where possible.

| Map syntax                                                                                                                                                                                  | Preview | Basic editing | New element | Graphical editing | Current behavior                                                                                                                                                              |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :-----: | :-----------: | :---------: | :---------------: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| File header, version, and encoding                                                                                                                                                          |    △    |       ✕       |      -      |         -         | Loads BVE Map 2.0+ files in UTF-8/BOM, UTF-16LE/BE, and CP932/Shift_JIS-related encodings; arbitrary declared encodings are not supported                                     |
| Comments and basic statement syntax                                                                                                                                                         |    √    |       ✕       |      -      |         -         | Supports `#`/`//` comments, semicolon-separated calls, keyed/nested elements, whitespace, multiline statements, and case-insensitive names; there is no general source editor |
| Variables in assignments, arguments, and keys                                                                                                                                               |    √    |       ✕       |      -      |         -         | Evaluated during parsing and shown in a read-only, case-insensitively grouped assignment/source list                                                                          |
| Arithmetic operators (`+`, `-`, `*`, `/`, `%`)                                                                                                                                              |    √    |       ✕       |      -      |         -         | Supports numeric arithmetic, unary signs, parentheses, and `+` string concatenation; comparison/logical and compound assignment operators are unsupported                     |
| Distance declarations and `distance` expressions                                                                                                                                            |    √    |       △       |      -      |         ✕         | Existing supported element distances are editable; insertion may create/reuse a distance block, but there is no standalone distance editor                                    |
| Mathematical functions                                                                                                                                                                      |    √    |       ✕       |      -      |         -         | Supports `rand`, `abs`, `sin`, `cos`, `atan2`, `sqrt`, `exp`, `log`, `floor`, `ceil`, and `pow`                                                                               |
| `include 'file';`                                                                                                                                                                           |    √    |       △       |      ✕      |         -         | Nested Include files participate in parsing and supported contained elements can be written back; a missing or invalid child map is skipped with a warning; with Edit enabled, the File Structure Diagram context menu can change an Include reference to another .txt/.csv file (relative path preferred, absolute fallback) or remove it, and edits are blocked when surviving statements still depend on that file |
| `Curve.SetGauge(value)` / `[legacy] Curve.Gauge(value)`                                                                                                                                     |    √    |       ✕       |      ✕      |         ✕         | Updates the own-track gauge used for cant geometry; no editable element row is created                                                                                        |
| `Curve.SetCenter(x)`                                                                                                                                                                        |    √    |       ✕       |      ✕      |         ✕         | Updates the own-track cant rotation center; no editable element row is created                                                                                                |
| `Curve.SetFunction(id)`                                                                                                                                                                     |    √    |       ✕       |      ✕      |         ✕         | Selects sinusoidal or linear curve/cant interpolation; no editable element row is created                                                                                     |
| `Curve.BeginTransition()`                                                                                                                                                                   |    √    |       △       |      △      |         ✕         | The consolidated `Curve.*` form adds it only before a canted `Curve.Begin` or selected `Curve.End`; paired start/end transition options stay linked and keep separate distances |
| `Curve.Begin(radius, cant)` / `[legacy] Curve.BeginCircular(radius, cant)`                                                                                                                  |    √    |       √       |      △      |         ✕         | The `Curve.*` form emits current `Curve.Begin(radius, cant)` only with a preceding transition at its own distance; `Curve.BeginCircular` remains edit-only                  |
| `Curve.Begin(radius)` / `Curve.Change(radius)`                                                                                                                                              |    √    |       √       |      √      |         ✕         | The `Curve.*` form selects current Begin or Change and can atomically add the supported end position; existing rows retain their method                                     |
| `Curve.End()`                                                                                                                                                                               |    √    |       √       |      √      |         ✕         | The `Curve.*` form adds End alone or with a start position, optionally preceded by a transition at its own distance; existing rows remain editable/deletable                |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()`                                                                                                     |    √    |       ✕       |      ✕      |         ✕         | All official 0/1/2-argument forms feed geometry, but are not exposed as editable curve rows                                                                                   |
| `Gradient.BeginTransition()`                                                                                                                                                                |    √    |       △       |      △      |         ✕         | The consolidated `Gradient.*` form adds it before selected Begin/End positions; paired transition options stay linked and keep separate distances                           |
| `Gradient.Begin(gradient)` / `[legacy] Gradient.BeginConst(gradient)`                                                                                                                       |    √    |       √       |      √      |         ✕         | The `Gradient.*` form adds current `Gradient.Begin(gradient)` alone or with End, optionally with a preceding transition; `Gradient.BeginConst` remains edit-only           |
| `Gradient.End()`                                                                                                                                                                            |    √    |       √       |      √      |         ✕         | The `Gradient.*` form adds End alone or with Begin, optionally with a preceding transition at its own distance; existing rows remain editable/deletable                    |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()`                                                                                                                                 |    √    |       ✕       |      ✕      |         ✕         | Both official 0/1-argument forms feed geometry, but are not exposed as editable gradient rows                                                                                 |
| `Legacy.Turn`, `Legacy.Curve`, `Legacy.Pitch`                                                                                                                                               |    √    |       △       |      ✕      |         ✕         | Project compatibility syntax: all feed own-track geometry; only existing `Legacy.Curve` rows have source-backed value/distance editing                                        |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()`                                                                         |    √    |       △       |      √      |         ✕         | The wizard creates all current forms. Existing distance/numeric fields and deletion are supported; `trackKey` stays read-only in Properties/Edit but can be renamed for the whole track from Other Tracks |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()`                                                                         |    √    |       △       |      √      |         ✕         | The wizard creates all current forms; existing rows retain the same editing boundary as X interpolation                                                                       |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)`                                                           |    √    |       △       |      √      |         ✕         | The wizard creates all current forms; existing numeric fields/distance and deletion are supported, but statement shape and track key are read-only                            |
| `Track[trackKey].Cant.SetGauge(gauge)` / `[legacy] Track[trackKey].Gauge(gauge)`                                                                                                            |    √    |       △       |      √      |         ✕         | The wizard creates only the current `Cant.SetGauge` form. Legacy `Gauge` remains readable/editable/deletable with method/key read-only                                       |
| `Track[trackKey].Cant.SetCenter(x)`                                                                                                                                                         |    √    |       △       |      √      |         ✕         | The wizard creates the current form; existing value/distance and deletion are supported, with method/key read-only                                                           |
| `Track[trackKey].Cant.SetFunction(id)`                                                                                                                                                      |    √    |       △       |      √      |         ✕         | The wizard creates the current form with id `0` or `1`; existing value/distance and deletion are supported, with method/key read-only                                        |
| `Track[trackKey].Cant.BeginTransition()`                                                                                                                                                    |    √    |       △       |      √      |         ✕         | The wizard creates the current form; existing distance and deletion are supported, with method/key read-only                                                                 |
| `Track[trackKey].Cant.Begin(cant)`                                                                                                                                                          |    √    |       △       |      √      |         ✕         | The wizard creates the current form; existing value/distance and deletion are supported, with method/key read-only                                                           |
| `Track[trackKey].Cant.End()`                                                                                                                                                                |    √    |       △       |      √      |         ✕         | The wizard creates the current form; existing distance and deletion are supported, with method/key read-only                                                                 |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / `[legacy] Track[trackKey].Cant(cant)`                                                                     |    √    |       △       |      √      |         ✕         | The wizard creates only the current 0/1-argument forms. Legacy `Cant` remains readable/editable/deletable with shape/key read-only                                          |
| `Structure.Load(filePath)`                                                                                                                                                                  |    √    |       △       |      ✕      |         -         | Existing loaded-list key/path rows support inline edit, clear, reorder, delete, and right-click insertion above/below; inserted rows use two CSV fields. Edit mode can replace the Load path with a loader-compatible `BveTs Structure List` (1.00+) |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                                                                                    |    √    |       √       |      √      |         △         | All fields can be written back or created; 3D directly manipulates X/Y/Z only                                                                                                 |
| `Structure[structureKey].Put0(trackKey, tilt, span)`                                                                                                                                        |    √    |       √       |      √      |         △         | `Properties/Edit` can add/remove coordinate offsets to convert between `Put0` and `Put`; `Put0` uses a Z-only whole-metre distance gizmo                                      |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)`                                                               |    √    |       √       |      √      |         △         | Both official forms preview/edit; Inspector drafts update the deformed 3D vertices live, and a Z-only gizmo changes `distance` in whole-metre steps                            |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` |    √    |       △       |      √      |         △         | The wizard creates Begin/Begin0 alone or atomically with End; a confirmed Begin-only overlap becomes a change point, while paired interval overlaps are rejected. `Properties/Edit` also offers `Insert Change Point`, which opens the source-matched Begin/Begin0 form with Begin-only selected and current inspector drafts prefilled. `repeaterKey` renames, linked deletion/trim, form conversion, and gizmos are supported |
| `Repeater[repeaterKey].End()`                                                                                                                                                               |    √    |       △       |      √      |         △         | The wizard can create End alone or with Begin/Begin0. An open Repeater's `Properties/Edit` opens an End-only form with its source file and key prefilled. It allows isolated End statements but rejects an End inside a same-name interval that is already explicitly closed; existing End distance and linked deletion/trim remain supported |
| `Background.Change(structureKey)`                                                                                                                                                           |    √    |       √       |      √      |         ✕         | Distance/key can be edited, created, or deleted; it feeds background data and the scene preview                                                                               |
| `Station.Load(filePath)`                                                                                                                                                                    |    √    |       △       |      ✕      |         -         | Existing station-definition rows support inline edit, clear, reorder, delete, and right-click insertion above/below; inserted rows use 13 CSV fields. When an edited or inserted row is serialized, blank `stoppageTime`, `signalFlag`, `alightingTime`, `passengers`, `doorReopen`, and `stuckInDoor` fields are written as `0`; empty keys, names, times, and sound keys remain empty. With Edit enabled, the Load path can be replaced with a loader-compatible `BveTs Station List` (existing 0.04+ compatibility) |
| `Station[stationKey].Put(door, margin1, margin2)`                                                                                                                                           |    √    |       √       |      √      |         ✕         | Distance, key, and door side can be edited, created, or deleted. Stop tolerances require `margin1 < 0` and `margin2 > 0`; loaded violations warn with source location, while edits/creation are blocked |
| `Section.Begin(...)` / `[legacy] Section.BeginNew(...)`                                                                                                                                     |    √    |       √       |      √      |         ✕         | Distance and variable-length signal-index parameters support edit, creation, and deletion; markers appear in 2D/3D                                                            |
| `Section.SetSpeedLimit(...)` / `[legacy] Signal.SpeedLimit(...)`                                                                                                                            |    √    |       √       |      √      |         ✕         | Distance and variable-length speeds support edit, creation, and deletion; active values feed the 3D signal summary                                                            |
| `Signal.Load(filePath)`                                                                                                                                                                     |    √    |       △       |      ✕      |         -         | Aspect/glare rows support inline edit, clear, reorder, delete, and right-click insertion above/below. New primary rows have six CSV fields; main/glare blocks remain together and `Add Glare` is explicit. New aspect columns and more than 509 visible structure-key columns are unsupported |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                  |    √    |       √       |      √      |         △         | Both official forms edit; the wizard emits the full form. Short-form extended edits require confirmed conversion; 3D directly manipulates X/Y/Z                               |
| `Beacon.Put(type, section, sendData)`                                                                                                                                                       |    √    |       √       |      √      |         ✕         | Distance and all arguments can be edited, created, or deleted                                                                                                                 |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()`                                                                                                                                                  |    √    |       √       |      √      |         ✕         | Independent Begin/End points support edit, creation, and deletion without pairing or type conversion                                                                          |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)`                                                                                                                                             |    √    |       ✕       |      ✕      |         ✕         | Feeds the read-only list and map markers; no source-backed edit or insertion                                                                                                  |
| `Light.Ambient(...)`, `Light.Diffuse(...)`, `Light.Direction(...)`                                                                                                                          |    √    |       √       |      √      |         ✕         | The Lighting Effects tab supports source-backed RGB/pitch/yaw editing and deletion. The Effects wizard creates each official form in the selected source file at distance `0` (without a distance input). RGB must be in `[0, 1]`; Direction must be at distance `0`; duplicate declarations of one kind anywhere in the root map or Includes invalidate that kind and report every source location. No markers or 3D lighting simulation are implemented |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / `[legacy] Fog.Set(density, red, green, blue)`                                             |    √    |       √       |      √      |         ✕         | All official 0/1/4-argument Interpolate forms and the legacy Set form support edit, creation, and deletion; 3D applies interpolated exponential fog                           |
| `[legacy] Legacy.Fog(start, end, red, green, blue)`                                                                                                                                          |    √    |       ✕       |      ✕      |         ✕         | Legacy linear-fog statement still accepted by BVE; read-only rows appear in a dedicated list and plan/scene markers with their source values; editing, creation, and the 3D fog effect are not implemented |
| `DrawDistance.Change(value)`                                                                                                                                                                |    √    |       √       |      √      |         ✕         | Distance/value support edit, creation, and deletion; the statement can optionally control scene draw distance                                                                 |
| `CabIlluminance.Interpolate(value)` / `CabIlluminance.Interpolate()` / `[legacy] CabIlluminance.Set(value)`                                                                                   |    √    |       √       |      √      |         ✕         | Distance/value support edit, creation, and deletion; a blank value writes `Interpolate()`, which inherits the previous explicit CabIlluminance value in table and 3D marker labels (blank when none); cab brightness is not simulated |
| `Irregularity.Change(x, y, r, lx, ly, lr)`                                                                                                                                                  |    √    |       √       |      √      |         ✕         | Distance and all six values support edit, creation, and deletion; vehicle vibration is not simulated                                                                          |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)`                                                                                                                                           |    √    |       √       |      √      |         ✕         | Both official shapes support edit, creation, and deletion; adhesion effects are not simulated                                                                                 |
| `Sound.Load(filePath)`                                                                                                                                                                      |    √    |       △       |      ✕      |         -         | Existing sound-list rows support inline edit, clear, reorder, delete, and right-click insertion above/below; inserted rows use three CSV fields. Edit mode can replace the Load path with a loader-compatible `BveTs Sound List` (2.00+) |
| `Sound[soundKey].Play()`                                                                                                                                                                    |    √    |       √       |      √      |         ✕         | Distance/key support edit, creation, and deletion; audio is not played                                                                                                        |
| `Sound3D.Load(filePath)`                                                                                                                                                                    |    √    |       △       |      ✕      |         -         | Existing 3D-sound-list rows support inline edit, clear, reorder, delete, and right-click insertion above/below; inserted rows use three CSV fields. Edit mode can replace the Load path with a loader-compatible `BveTs Sound List` (2.00+) |
| `Sound3D[soundKey].Put(x, y)`                                                                                                                                                               |    √    |       √       |      √      |         ✕         | Distance/key/X/Y support edit, creation, and deletion; the 3D tag points at the fixed source and its X/Y/Z gizmo edits X/Y or whole-metre distance; audio is not played         |
| `RollingNoise.Change(index)`                                                                                                                                                                |    √    |       √       |      √      |         ✕         | Distance/index support edit, creation, and deletion; audio is not played                                                                                                      |
| `FlangeNoise.Change(index)`                                                                                                                                                                 |    √    |       √       |      √      |         ✕         | Distance/index support edit, creation, and deletion; audio is not played                                                                                                      |
| `JointNoise.Play(index)`                                                                                                                                                                    |    √    |       √       |      √      |         ✕         | Distance/index support edit, creation, and deletion; audio is not played                                                                                                      |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)`                                                                                |    △    |       ✕       |      ✕      |         ✕         | Definitions are shown, but external other-train files are only partially modeled                                                                                              |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)`                                                                                                                           |    √    |       ✕       |      ✕      |         ✕         | The unique enable time is shown read-only above the matching other-train stop table                                                                                           |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)`                                                                                                                             |    √    |       ✕       |      ✕      |         ✕         | Generates read-only other-train stop tables, paths, and map markers                                                                                                           |

## Installation and Startup

This repository does not currently provide a standalone installer or prebuilt release package. Follow the [developer guide](docs/dev.md) to build the application, then run the generated executable.

After building, run `build_release\komapedit.exe`. The executable stays at the
top level, while `maploader.dll`, `model_loader.dll`, and copied Assimp/runtime
dependencies are loaded from `build_release\bin`.

On startup, the application creates the `settings` directory when necessary and
creates or reads the following files there:

- `settings/imgui.ini`: stores UI window positions and related ImGui layout data.
- `settings/settings.ini`: stores UI language, font/component/station-marker sizes, 2D line widths, theme color, edit-mode warning state, and 3D canvas options such as automatic scene-preview loading on map open, fog, draw distance, gizmo size, camera speed, and performance warnings.
- `settings/history.ini`: stores recent maps and background-image alignment parameters.

The settings readers accept only the exact sections, keys, and value forms
written by the current application. Obsolete aliases, values in the wrong
section, and loose value forms are ignored and use defaults. Loading an existing
partial or obsolete file does not rewrite it; an explicit settings save writes a
complete current-format file.

The bundled executable requires maploader API v9 and loads maps only through
`kv_load_map_ex()`. Earlier DLLs are rejected by the exact API-version check.
Build and distribution-cleanup scripts do not migrate or delete obsolete
root-level INIs or DLLs: if either is present, they stop and require a clean
`bin`/`settings` layout.

## Usage

This section follows the interface from top to bottom, then covers 2D, 3D, and finally the editing workflow. You do not need to enable `Enable Edit` just to view a map. Before changing files, read section 10 and back up the map.

### 1. Main Window Components

The main window contains these areas:

- **Top menu**: Opens files, shows or hides windows, changes 2D/3D settings, and opens help pages.
- **Toolbar**: Provides common actions such as Open, Reload, editing, Save, Station Jump, and mileage jump.
- **Central workspace**: Shows the 2D View, File Structure Diagram, Text Preview, Structure Model Preview, and 3D Scene Preview. Windows can be stacked as tabs or dragged to other docking positions.
- **Map information tables**: Usually docked on the right. They show stations, structures, signals, sounds, and other map elements.
- **Console window**: Usually docked at the lower right. It shows detailed messages from map loading, model loading, editing, and saving.
- **Bottom status bar**: Shows the current operation and the number of errors and warnings.

If you close a window, reopen it from `Map Info List`, `2D View`, `3D View`, or `Auxiliary Info`. Window visibility and docking layout are saved automatically.

### 2. Top Menu Functions

#### File

- `New...`: Opens the New File Wizard. It can create a map or resource-list file. See section 10 for details.
- `Open...`: Selects a `.txt` or `.csv` map or scenario file. The toolbar `Open` button does the same thing.
- `Recent Maps`: Opens a recently used map. `Clear List` removes only the history entries; it does not delete map files.
- `Reload`: Reads the current map again and reloads the current Structure Model Preview. The shortcut is `F5`. If there are unsaved changes, the program asks for confirmation first.
- `Export CSV...`: Selects a directory and exports geometry data for the own track and every other track. See the appendix for the CSV fields.
- `Exit`: Closes the program. If there are unsaved changes, you can save them, discard them, or cancel the exit.

When you open a `BveTs Scenario 2.00` scenario file, the scenario itself remains available as a read-only document:

- `Map Info List -> Other -> Scenario File` shows the scenario fields, the original Route/Vehicle paths, and their weights.
- If there is one valid Route, its map loads directly. If there are several candidates, you choose one.
- If Route is missing, its target does not exist, or you cancel the choice, you can still view the scenario file, but no map is loaded. The same degradation applies when the Route target exists but is not a valid BVE map. A missing Vehicle entry or a Vehicle path whose target does not exist never blocks loading the map of a valid Route; Vehicle data is preview-only.
- Editing, saving, reloading, and `Recent Maps` always use the resolved map file. Weights are validated but are not used to choose a Route automatically.

#### Options

- `UI Settings...`: Changes text size, UI component size, and the interface theme color.
- `2D Canvas Settings -> Canvas Element Sizes`: Changes marker sizes, own-track and other-track line widths, chart marker lines, and grid lines.
- `2D Canvas Settings -> Plot Range...`: Limits the mileage range currently shown.
- `2D Canvas Settings -> Control Points...`: Changes the range and interval used to sample track geometry.
- `3D Canvas Settings`: Changes scene draw distance, edit component size, camera speed, fog, map-driven draw distance, automatic loading, and performance warnings.

#### Map Info List

Opens tables grouped by stations, structures, track geometry, signals, sounds, effects, and other data. See section 6 for table details.

#### 2D View

- Shows or hides the whole 2D View window, Profile chart, and Curve Radius chart.
- Shows or hides gradient change points, gradient values, and other tracks in the Profile chart.
- Imports, shows, or adjusts a background image, or aligns it using two station positions. You can adjust its position, size, rotation, and brightness.

#### 3D View

- `Structure Model Preview`: Shows or hides the preview window for one model.
- `3D Scene Preview`: Shows or hides the scene preview for the whole map.

#### Auxiliary Info

- Controls groups of station, track-geometry, signal, sound, and effect markers in 2D and 3D.
- `Section Markers` is off by default. When enabled, green `S` markers and their signal-index parameters appear in 2D and 3D.
- `Own Track Markers` and `Show Current Position on Plan` control helper displays shared by the 3D scene and 2D plan.
- `Other -> File Structure Diagram`, `Text Preview`, and `Console` open the corresponding tool windows.

Signal markers are also controlled by the `Show` checkbox in each `Map Signal List` row. Other-train paths are controlled one at a time in the `Other Train List`.

#### Language and Help

- `Language`: Switches between Simplified Chinese, English, and Japanese.
- `Help`: Opens the online documentation, issue-reporting page, or About window.

### 3. Toolbar Functions

- **Open**: Selects and opens a map or scenario file.
- **Reload**: Reads the map again and reloads the current single-model preview. It is the same as pressing `F5`.
- **Reload Track Geometry**: Reads the map geometry again while keeping already loaded 3D scene models where possible. Use it after changing only route code.
- **Enable Edit**: Turns editing on or off. A risk warning appears the first time you enable it.
- **Add Map Element**: Opens the New Map Element Wizard. It is available only after editing is enabled and the edit metadata is ready.
- **Save**: Writes changes already applied to the in-memory preview back to the source files. The shortcut is `Ctrl+S`. Save is blocked while a resource-list table still has an unapplied draft.
- **Revert**: Discards all unsaved changes and restores the version on disk. The program asks for confirmation first.
- **Station Jump**: Selects a station and moves the 2D View to it. If the 3D scene is running, its camera also moves.
- **Jump to distance(m)**: Enter a number and click `Jump`, or press `Enter` in the input box. If the 3D scene is running, its camera also moves.

The currently implemented global shortcuts are `F5` for Reload and `Ctrl+S` for Save. Mouse and keyboard controls for the 2D and 3D canvases are listed in sections 5 and 9.

### 4. Bottom Status Bar and Console Functions

#### Bottom Status Bar

- `Err` and `Warn` on the left show the number of errors and warnings. Click this area to view a summary.
- The status text on the right shows states such as Loading, Ready, Applying changes, and Saved. After a long operation finishes, it also shows the elapsed time.

#### Console Window

The Console shows complete loading, parsing, model, and editing logs. Check it first when a map does not open, a model is missing, or saving fails.

- **Clear**: Removes all log messages and resets the error and warning counts.
- **Copy**: Copies the full log to the clipboard for use in a bug report.
- New messages scroll into view automatically only when the log is already at the bottom. If you scroll up, the Console does not force you back to the bottom.

### 5. 2D Canvas Functions

At the top of the 2D View, choose `Move` or `Measure` mode and set the grid to `Fixed`, `Movable`, or `None`. Use the vertical splitter to change the height of the plan and the charts below it. Use the horizontal splitter to change the widths of the Profile and Curve Radius charts.

#### Plan: Move Mode

| Action | Function |
| --- | --- |
| Drag with the left mouse button | Pan the plan |
| Scroll the mouse wheel | Zoom the plan |
| `Shift` + mouse wheel | Rotate the plan by 5° per step |
| `Ctrl` + left-mouse drag | Rotate continuously around the canvas center |
| Double-click the left mouse button | Fit the full map to the window |
| Right-click a marker | Open its locate, find, or preview menu; `Properties/Edit` and `Delete` are also shown when editing is enabled |

For touch input, drag with one finger to pan. Use two fingers to pan, zoom, and rotate at the same time. Long-pressing a marker is the same as right-clicking it.

#### Plan and Charts: Measure Mode

- Move the pointer near the track to see its mileage, elevation, gradient, curve radius, and current speed limit.
- Double-click the Plan, Profile, or Curve Radius chart to move all three measurement positions to that mileage.
- The mouse wheel on the Profile and Curve Radius charts changes only the horizontal mileage range.

#### Markers, Navigation, and Editing

- Use `Auxiliary Info` to choose which markers are visible. This avoids showing too much information at once on a large map.
- Right-click a marker to locate its table row. Items with a 3D object can also be located in the 3D scene.
- When editing is enabled, editable own-track curve and gradient change points and other-track change points are also shown. Right-click these markers to open `Properties/Edit` or delete them.
- In the Profile and Curve Radius charts, only correctly paired curve or gradient change points have an edit menu.

#### Background Image

After importing an image from `2D View -> Background Image`, you can show it, adjust it manually, or choose `Align Background to Stations`. For station alignment, select two stations and double-click their matching positions on the image. The program calculates the image position, scale, and rotation.

### 6. Table Functions

#### Common Actions

- Open the required table from `Map Info List`. Right-click a mileage cell that has a locate menu to move to that position in the Plan or a running 3D scene.
- Right-click a source-file or resource-file path to open its directory. Hover over a path to see the original argument and resolved absolute path when available.
- The Structure Model, Signal Aspect, Sound File, and 3D Sound File tables support partial or exact searches, previous and next results, and searches for unused entries.
- In a map-placement row, right-click a resource key to jump to the matching Structure Model, Signal Aspect, or sound definition.
- Without editing, tables provide only viewing, searching, navigation, and preview actions. With editing enabled, right-clicking an editable map element also shows `Properties/Edit` and `Delete`. Some tables also open `Properties/Edit` on double-click.

#### Stations and Structures

- **Station List**: The upper part shows `Station.Put` stop positions. The lower part shows station definitions loaded by `Station.Load`, including fields such as station name, stop time, and door side.
- **Map Structure List**: Shows `Structure.Put` and `Structure.Put0`. You can locate an item in the Plan, the 3D scene, or its model definition.
- **Map Structure List (PutBetween)**: Shows `Structure.PutBetween` structures deformed between two tracks.
- **Structure Model List**: Shows each structureKey and model path. Right-click a key to preview the model or fill that key into an open New Map Element Wizard of a matching type.
- **Repeater List**: Combines related `Repeater.Begin`/`Begin0`/`End` statements into intervals. You can jump to a start, end, or change point. If an interval has several Begin statements, the delete menu offers `Delete All`, `Delete Change Point`, `Trim to Change Point`, and `Start from Change Point`.
- **Other Train List**: Shows other-train definitions, stop positions, and the read-only `Train.Enable` time. You can control each path separately and locate stop positions in the Plan.

#### Track Geometry, Signals, and Sections

- **Other Tracks**: Controls the visibility, mileage range, and color of each other track. With editing enabled, right-click `Key` to rename matching `Track[...]` statements. References to that track in Structure, Signal, and Repeater statements are not renamed automatically.
- **Track Irregularity, Adhesion Change Point, and Speed Limit Point lists**: Show the corresponding positions and can locate them in 2D or 3D. Speed-limit Begin and End may exist independently; End edits only its mileage.
- **Signal Aspect List**: Shows signal aspect definitions and their structure keys. You can jump from a structure key to its model.
- **Map Signal List**: Shows `Signal.Put` positions and parameters. Each row's `Show` checkbox controls its Plan marker.
- **Section List**: Shows `Section.Begin`/`BeginNew` separately from `Section.SetSpeedLimit`/`Signal.SpeedLimit`, including variable-length parameters and explicit `null` values.
- **Beacon List**: Shows and locates `Beacon.Put` statements.
- **Variable List**: Groups all assignments by case-insensitive variable name. Hover to see the original expression. This table is read-only.

#### Sounds and Effects

- **Sound File List and 3D Sound File List**: Show sound keys, file paths, and buffer counts. You can open a file's directory or fill its key into a matching New Map Element Wizard.
- **Map Sound, Map 3D Sound, Rolling Noise Change Point, Flange Noise Change Point, and Joint Noise Play Point lists**: Show and locate playback or change positions.
- **Background, Cab Illuminance, Fog, Legacy Fog, and Draw Distance Change Point lists**: Show and locate the corresponding effects.
- **Scenario File**: Available only when a Scenario document is open. It shows every official scenario field and is read-only.

#### Inline Editing in Resource Lists

Station definitions, Structure Models, Signal Aspects, Sound Files, and 3D Sound Files use the same inline editing controls:

- Double-click an editable cell to type, then press `Enter` to finish that cell. Right-click to select a file, insert a row above or below, move the whole row up or down, clear the cell, or delete the whole row.
- A list that only carries its file header shows an `Add Row` button; inserting into it appends the first row after the header, so no comma placeholder line is needed.
- `Select File` stores a relative path where possible and an absolute path otherwise.
- A Signal primary row and its glare row form one block. A new primary row always has six fields and no glare; use `Add Glare` when needed.
- `Apply` in a table submits only that table's drafts to the in-memory preview. The toolbar `Save` is available only after every resource-list draft has been applied.

A map can load only one resource list of each type. If a list is not specified, the table shows `New or Import File`. To replace an existing list, right-click `Source path` at the top of its table and choose `Change File...`. If that list has unapplied or unsaved edits to the old file, the program asks for confirmation and discards only that list's old drafts.

When importing or replacing a resource list, the program checks its header and version. Station List uses the existing 0.04+ compatibility rule, Structure List requires 1.00+, and Signal Aspects List and both Sound Lists require 2.00+. Resource lists support `#` comments. For compatibility with existing routes, unquoted `//` also starts a comment; `//` inside a double-quoted CSV field remains normal text.

The map is rejected if the entry map and its Includes load the same unkeyed resource-list type more than once, or contain duplicate case-insensitive `Train[].Enable` statements. The reason is shown in the Console.

### 7. File Structure Diagram Functions

Open this window from `Auxiliary Info -> Other -> File Structure Diagram`. The entry map is on the left, and its included submaps are shown by level to the right. Hover over a node to see the Include argument and absolute path.

#### Viewing Files

- Right-click a valid node and choose `Preview Text` to view its source and line numbers in the read-only Text Preview.
- `Open in File Explorer` opens the target file's directory. If the file is missing, it still tries to open the target directory. If that directory is also missing, an error is reported in the Console.
- Missing or invalid Include targets are red. Text preview, submap import, and new-submap actions are disabled for them.

#### Editing Includes

The following actions require editing to be enabled:

- **Change Included File...**: Selects a new `.txt` or `.csv` submap and rewrites the Include path in its parent file. A relative path is preferred; an absolute path is used if needed.
- **Unlink Include**: Deletes the Include statement from its parent. The action is blocked if later statements still depend on data from that submap.
- **Import Submap...**: Selects an existing BVE map and stages a new Include in the source file represented by the current node.
- **New Submap...**: Creates a blank submap with the `BveTs Map 2.02:utf-8` header, UTF-8 without BOM, and CRLF line endings, then stages its Include. It does not overwrite an existing file.

A new Include is placed after the last existing Include before the first local distance statement. If there is no suitable Include, it is placed before the first distance statement. If neither exists, it is appended to the file. These changes refresh the in-memory preview immediately, but only `Save` writes them to the parent map.

### 8. 3D Model Preview Functions

Open `3D View -> Structure Model Preview`, then right-click a structureKey in the Structure Model List and choose `Preview Model`.

- **Model List**: Opens the Structure Model List.
- **Reload**: Reads the current model, materials, and textures again.
- **Clear**: Removes the current model from the preview.
- **Background Color**: Selects any RGB color or uses the white, black, gray, blue, or green shortcut colors.
- Drag with the left mouse button to rotate the model. Scroll the mouse wheel to zoom.

If a model or texture cannot be loaded, the window remains usable and the detailed warning or error appears in the Console.

### 9. 3D Scene Preview Functions

Open `3D View -> 3D Scene Preview`, then click `Start 3D Scene Preview`. If `Options -> 3D Canvas Settings -> Automatically load scene preview when opening a map` is enabled, opening or reloading a map starts the preview automatically. This option is off by default.

The top of the window has `Start 3D Scene Preview`, `Reload (Models)`, and `Close`:

- `Reload (Models)` reads the scene models again while keeping the current map and camera position.
- `Close` releases the current scene but can leave the preview window open.

#### Move Mode and Shortcuts

The following controls are available while the pointer is over the 3D canvas.

Keyboard movement works in `Move`, `Select`, and `Mileage Select` modes. Dragging with the left mouse button to turn the view works only in `Move` mode.

| Action | Function |
| --- | --- |
| Drag with the left mouse button | Turn the view in `Move` mode |
| `W` / `S` | Move forward or backward along the route |
| `A` / `D` | Move left or right |
| `R` / `F` | Move up or down |
| Hold `Ctrl` | Move faster with the keys above |
| `X` | Reset the camera to its default pose above the own-track center at the current mileage |

Set the camera speed in `3D Canvas Settings`. Toolbar station and mileage jumps also move the scene camera. If `Show Current Position on Plan` is enabled, the 2D Plan also shows the camera position.

#### Select Mode

- Move the pointer over a scene object or marker to highlight it.
- Right-click an object or marker to locate its Map Info table. For a Repeater, you can also jump to the start or end/change position.
- With editing enabled, the same menu also provides `Properties/Edit` and delete actions. See section 10 for edit gizmos.

#### Mileage Select Mode

- The nearest whole-metre cross-section on the own-track plane and its mileage label appear near the pointer.
- With editing enabled, right-click this position and choose `Add Map Element at Current Mileage`. The wizard fills in `distance` automatically.

#### Scene Overlay and Settings

The canvas shows the camera offset, height, and mileage; current curve radius and cant; gradient; speed limit; section signal speed; and next-station information. The bottom also shows scene chunks, instances, loaded models, and frame rate.

`3D Canvas Settings` can immediately toggle fog, map-driven draw distance, and performance warnings, and can change the normal draw distance. Related marker visibility stays synchronized with `Auxiliary Info`.

### 10. Editing, the New Map Element/New File Wizards, Applying Changes, and Saving

#### Before Editing

Editing is still experimental. The first time you enable `Enable Edit` on the toolbar, a risk warning appears. Back up the map first, or keep a recoverable version with a version-control tool such as Git. Select `Don't show again` and confirm to hide future warnings.

If you disable editing, open another document, reload, or exit while changes are unsaved, the program asks for confirmation. `Apply` and `Save` are different operations.

#### Drafts, Apply, Save, and Revert

Editing has three stages:

1. **Window draft**: You have entered values in `Properties/Edit`, a resource-list table, or a wizard. The map may show only some live draft effects, and no source file has changed.
2. **Apply to preview**: Click `Apply` in the window. The program validates and reparses its in-memory working copy, then refreshes the 2D View, tables, and 3D preview. Files on disk still have not changed.
3. **Save to disk**: Click toolbar `Save` or press `Ctrl+S`. Only now are all applied changes written to their map, Include, or resource-list source files.

Toolbar `Revert` discards every unsaved change and restores the disk version. `Reload` also reads the files from disk again, but first asks before discarding unsaved changes. If a resource list still has an unapplied draft, click `Apply` in that table before saving.

Before saving, the program reparses all affected files and checks the intended result, file encoding, and outside changes to files on disk. If new text cannot be represented in the original encoding, or another program has changed a file, Save is blocked instead of silently changing the encoding or overwriting the outside change.

#### Editing or Deleting Existing Map Elements

1. Right-click an item in a table or a 2D/3D marker and choose `Properties/Edit`.
2. Check the source file, source position, and raw statement in the window, then change the fields.
3. Click `Apply` to refresh the in-memory preview.
4. Check the 2D View, tables, and 3D result. If they are correct, use toolbar `Save`.

Editable items include station, structure, signal, and beacon placements; Repeaters; Sections; speed limits; curves and gradients; other-track changes; track irregularity; map sounds and noises; backgrounds; adhesion; cab illuminance; fog; and draw distance. Deletion also enters the in-memory preview first and removes the statement from the source file only after Save.

The `Properties/Edit` window for `Structure.Put`/`Put0` and `Repeater.Begin`/`Begin0` provides `Add Coordinate Offsets` and `Remove Coordinate Offsets`. Removing nonzero offsets discards all six offset values, so the program asks for confirmation. A short-form `Signal.Put` also asks for confirmation before it is converted to the full form needed to edit Z, rotation, tilt, or span.

#### Live Adjustment in the 3D Scene

After opening `Properties/Edit`, supported objects show an edit gizmo. Dragging changes only the current window draft. You must still click `Apply` and then `Save`.

- `Structure.Put`, full-form `Signal.Put`, and `Repeater.Begin` use X/Y/Z axes to change coordinates.
- For `Sound3D[soundKey].Put(x, y)`, X/Y change the relative position in 0.001 m steps, and Z changes `distance` in whole metres.
- `Structure.Put0` and `Repeater.Begin0` show only a Z axis and change mileage in whole metres. Adding coordinate offsets in the Inspector switches to the full coordinate gizmo.
- A Repeater with an explicit End also shows a Z axis at its end position and changes EndDistance in whole metres.
- `Structure.PutBetween` uses a Z axis to change whole-metre mileage and recalculates the deformed model live.

#### New Map Element Wizard

Open the wizard from toolbar `Add Map Element`, or right-click the current mileage in the 3D scene's `Mileage Select` mode. First choose the target source file, then choose a template and enter its parameters. Every loaded non-resource-list map source file, including a blank or distance-free one, is eligible; when the selected source has zero or one numeric distance statement, insertion appends a canonical distance block after its existing text instead of moving existing statements.

The wizard provides the currently supported elements in these categories:

- **Structures**: `Structure.Put`, `Put0`, `PutBetween`, and `Repeater.Begin`/`Begin0`/`End`.
- **Station**: `Station.Put`.
- **Track Geometry**: Curves, gradients, track irregularity, and adhesion changes.
- **Other Tracks**: Current `Track.*` forms for position, X/Y interpolation, and cant.
- **Signal**: `Signal.Put`, speed-limit Begin/End, Section, signal speed, and `Beacon.Put`.
- **Sound**: Map sounds, 3D sound sources, rolling noise, flange noise, and joint noise.
- **Effects**: Background, cab illuminance, fog, and draw distance.

`Apply` creates the element and refreshes the preview. `Apply and Edit` closes the wizard after a successful Apply and opens `Properties/Edit` for the primary new statement. A change-point wizard opened from a Repeater Inspector does not offer `Apply and Edit`.

Important rules:

- A Repeater can add Begin, End, or both at once. When both are added, End mileage cannot be less than Begin mileage. A change point can be inserted inside an active same-name interval only under the supported rules; normal intervals cannot overlap.
- `Insert Change Point` in Repeater `Properties/Edit` copies the current draft parameters and fills both mileage fields with the current Begin mileage so you can adjust them.
- The Curve wizard supports `Curve.Begin(radius)`, `Curve.Change(radius)`, and `Curve.End()`. The Gradient wizard supports `Gradient.Begin(gradient)` and `Gradient.End()`. An optional transition start and its statement are created together in the correct source order. Legacy aliases and Interpolate forms cannot be created.
- Other-track templates generate only current `Track.*` forms. Optional trailing arguments must be enabled in order; for example, `radiusH` is required before `radiusV`. A new normalized trackKey creates another track. Numeric keys and quoted string keys remain distinct.
- If the wizard cannot choose a safe source insertion point automatically, Text Preview highlights parser-approved boundaries for you to choose from.

#### New File Wizard

Open it from `File -> New...`. It can create six file types: `BveTs Map 2.02`, Structure List, Signal Aspects List, Sound List, the Sound List used by 3D Sound, and Station List.

1. Choose the file type, enter a file name, choose `.txt` or `.csv`, and select a directory.
2. For a resource list, you can first click `Import File` to fill the form with an existing `.txt` or `.csv` file's name, directory, and suffix. You can still edit these fields.
3. To make the current map reference the file, select a target source file under `Reference in`. This requires editing to be enabled. Every loaded non-resource-list map source file is a candidate; official BVE does not require a distance statement for `include` or `*.Load`, so distance-free blank maps are listed as well.
4. Click `Confirm` to create or reuse the file. Only the map template provides `Confirm and Load`.

The selected suffix is always appended to the entered name. If the target is an existing regular file, it is reused without changing its contents. Otherwise, the wizard creates it with the standard header. A reference is first applied only to the current map's in-memory preview: maps use `include`, and resource lists use the matching `*.Load`. Use toolbar `Save` to write the reference. A map can reference only one resource list of each type. If a reference already exists, the wizard disables another one and tells you to replace the file from the `Source path` context menu at the top of the matching table.

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

| Field            | Description                                                                        |
| ---------------- | ---------------------------------------------------------------------------------- |
| distance         | Absolute map distance, in meters                                                   |
| x                | Calculated own-track plan X coordinate after gradient projection                   |
| y                | Calculated own-track plan Y coordinate after gradient projection                   |
| z                | Elevation                                                                          |
| direction        | Track direction angle, in radians                                                  |
| radius           | Current curve radius                                                               |
| gradient         | Current gradient, using BVE's per-mille convention                                 |
| interpolate_func | Interpolation type: `0` means sinusoidal half-wave easing, `1` means linear easing |
| cant             | Cant                                                                               |
| center           | Track center offset                                                                |
| gauge            | Track gauge                                                                        |

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

| Field            | Description                                                                   |
| ---------------- | ----------------------------------------------------------------------------- |
| distance         | Absolute map distance, in meters                                              |
| x                | Calculated other-track plan X coordinate derived from the projected own track |
| y                | Calculated other-track plan Y coordinate derived from the projected own track |
| z                | Other-track elevation                                                         |
| interpolate_func | Interpolation type: `0` means `sin`, `1` means `line`                         |
| cant             | Cant                                                                          |
| center           | Track center offset                                                           |
| gauge            | Track gauge                                                                   |

Exported numeric values use fixed six-decimal formatting. CSV export currently includes track geometry only; it does not export stations, Structures, repeaters, signals, beacons, sound/noise events, track-irregularity or adhesion changes, background change points, cab-illuminance change points, fog, draw-distance changes, or 3D scene data.

## License and Third-Party Notices

komapedit is distributed under the Apache License, Version 2.0. See `LICENSE`
for the license text and `NOTICE` for project attribution notices.

This project is based on `kobushi-trackviewer` and is intended to help inspect
and edit BVE Trainsim map files.

Reference project:

| Project                                                                                | Copyright                          | License                     |
| -------------------------------------------------------------------------------------- | ---------------------------------- | --------------------------- |
| [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) by konawasabi | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

Third-party libraries used by the GUI and model preview:

| Library                                                                | Use                                                                    | Copyright                                                                        | License                       |
| ---------------------------------------------------------------------- | ---------------------------------------------------------------------- | -------------------------------------------------------------------------------- | ----------------------------- |
| [Dear ImGui](https://github.com/ocornut/imgui)                         | Docking GUI, Win32 backend, DirectX 11 backend, C++ std::string helper | Copyright (c) 2014-2026 Omar Cornut                                              | MIT License                   |
| [ImPlot](https://github.com/epezent/implot)                            | 2D plotting widgets                                                    | Copyright (c) 2020-2024 Evan Pezent; Copyright (c) 2025-2026 Breno Cunha Queiroz | MIT License                   |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | Structure model import                                                 | Copyright (c) 2006-2026, assimp team                                             | Modified BSD 3-Clause License |
| stb single-file libraries bundled with Dear ImGui                      | Font/text/rectangle-packing support used by Dear ImGui                 | Copyright (c) 2017 Sean Barrett                                                  | MIT License or Public Domain  |

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
