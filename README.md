<p align="center">
    <img src="icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit is a lightweight Windows viewer and editor for BVE Trainsim map files, with 2D route views, 3D scene previews, and source-backed map editing.

Editing is experimental. Back up route files or keep them under version control before use. See [Current BVE Map Syntax Support](#current-bve-map-syntax-support) for supported statements and [TODO.md](TODO.md) for development progress.

## Documentation

- [简体中文README](docs/README_zhcn.md)
- [Development status (Todo List)](TODO.md)
- [Developer guide](docs/dev.md) ([简体中文版](docs/dev_zhcn.md))
- [AI-assisted development guide](docs/ai-dev.md) ([简体中文版](docs/ai-dev_zhcn.md))
- [Repository instructions for AI coding tools](AGENTS.md)
- [License](LICENSE), [project notice](NOTICE), and [third-party notices](THIRD_PARTY_NOTICES.md)

## Features

- **Interface**: Movable, dockable windows, searchable data tables, and navigation between tables, 2D views, and the 3D scene.
- **Preview**: Route plans, elevation profiles, markers, Structure models, and 3D scenes, with background-image alignment, measurement, and track-geometry CSV export.
- **Editing**: Modify, delete, or add supported map elements through property windows, resource lists, and wizards; adjust some elements' positions with 3D gizmos. Manage submaps and create or edit Map, Scenario, and resource-list files.
- **UI languages**: Simplified Chinese, English, and Japanese.


## Current BVE Map Syntax Support

- Preview: displays data through track geometry, tables, markers, or the 3D scene.
- Basic editing: change existing statements through Properties/Edit or tables and save them.
- New element: the New Map Element wizard inserts the corresponding source statement.
- Graphical editing: drag elements or use gizmos directly on the 2D/3D canvas.
- √ = fully supported; △ = partially or indirectly supported; ✕ = currently unsupported; - = support is not planned or not applicable.

The table covers [official BVE map syntax](https://bvets.net/jp/edit/formats/route/map.html), legacy aliases, and the project's `Legacy.*` compatibility statements. New element refers to the map-element wizard; use the New File wizard for files and their `Include`/`Load` references, and the corresponding tables to add resource-list rows.

| Map syntax                                                                                                                                                                                  | Preview | Basic editing | New element | Graphical editing | Notes |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :-----: | :-----------: | :---------: | :---------------: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| File header, version, and encoding                                                                                                                                                          |    △    |       ✕       |      -      |         -         | Supports BVE Map 2.0+ and UTF-8 (with or without BOM), UTF-16LE/BE, and CP932/Shift_JIS encodings |
| Comments and basic statement syntax                                                                                                                                                         |    √    |       ✕       |      -      |         -         | Supports `#`/`//` comments, semicolon separation, keyed and nested elements, and multiline statements; names are case-insensitive |
| Variables in assignments, arguments, and keys                                                                                                                                               |    √    |       ✕       |      -      |         -         | Evaluated during parsing; the variable table groups assignments and source locations by name |
| Arithmetic operators (`+`, `-`, `*`, `/`, `%`)                                                                                                                                              |    √    |       ✕       |      -      |         -         | Supports numeric arithmetic, unary signs, parentheses, and `+` string concatenation |
| Distance declarations and `distance` expressions                                                                                                                                            |    √    |       △       |      -      |         ✕         | Edit distance through supported elements; new elements create or reuse distance blocks |
| Mathematical functions                                                                                                                                                                      |    √    |       ✕       |      -      |         -         | Supports `rand`, `abs`, `sin`, `cos`, `atan2`, `sqrt`, `exp`, `log`, `floor`, `ceil`, and `pow` |
| `include 'file';`                                                                                                                                                                           |    √    |       △       |      ✕      |         -         | Supports nested Includes and editing elements in child maps; the File Structure Diagram can replace or remove references after dependency checks. Missing or invalid child maps are skipped with a warning |
| `Curve.SetGauge(value)` / `[legacy] Curve.Gauge(value)`                                                                                                                                     |    √    |       √       |      √      |         ✕         | Edit distance and gauge; the wizard creates `SetGauge` with default `1.067`; `CG` boards can be shown in 2D/3D |
| `Curve.SetCenter(x)`                                                                                                                                                                        |    √    |       √       |      √      |         ✕         | Edit distance and cant-center offset; the creation default is `0`; `CC` boards can be shown in 2D/3D |
| `Curve.SetFunction(id)`                                                                                                                                                                     |    √    |       √       |      √      |         ✕         | Edit distance and interpolation function with `id` set to `0` or `1`; the creation default is `0`; `CF` boards can be shown in 2D/3D |
| `Curve.BeginTransition()`                                                                                                                                                                   |    √    |       △       |      △      |         ✕         | The `Curve.*` form adds a preceding transition with a canted Begin or an End; start/end transition options are linked, with separate distances |
| `Curve.Begin(radius, cant)` / `[legacy] Curve.BeginCircular(radius, cant)`                                                                                                                  |    √    |       √       |      △      |         ✕         | The wizard creates a canted Begin together with a preceding transition; existing legacy BeginCircular statements can be edited |
| `Curve.Begin(radius)` / `Curve.Change(radius)`                                                                                                                                              |    √    |       √       |      √      |         ✕         | The wizard offers Begin or Change and can add an end position at the same time |
| `Curve.End()`                                                                                                                                                                               |    √    |       √       |      √      |         ✕         | Create alone or with a start position, optionally with a preceding transition |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()`                                                                                                     |    √    |       √       |      √      |         ✕         | Supports 0/1/2 arguments, preserving the argument count during editing; enable Curve Radius to edit or delete through 2D/3D markers |
| `Gradient.BeginTransition()`                                                                                                                                                                |    √    |       △       |      △      |         ✕         | The `Gradient.*` form adds a preceding transition with Begin/End; start/end transition options are linked, with separate distances |
| `Gradient.Begin(gradient)` / `[legacy] Gradient.BeginConst(gradient)`                                                                                                                       |    √    |       √       |      √      |         ✕         | The wizard can add Begin alone or together with End and preceding transitions; existing legacy BeginConst statements can be edited |
| `Gradient.End()`                                                                                                                                                                            |    √    |       √       |      √      |         ✕         | Create alone or with Begin, optionally with a preceding transition |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()`                                                                                                                                 |    √    |       ✕       |      ✕      |         ✕         | Both 0/1-argument forms feed track geometry |
| `Legacy.Turn`, `Legacy.Curve`, `Legacy.Pitch`                                                                                                                                               |    √    |       △       |      ✕      |         ✕         | All three statements feed own-track geometry; existing Legacy.Curve values and distances can be edited |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()`                                                                         |    √    |       △       |      √      |         ✕         | Supports all listed argument forms; edit existing distances and values or delete statements. `trackKey` is read-only in Properties/Edit; rename the whole track from Other Tracks |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()`                                                                         |    √    |       △       |      √      |         ✕         | Supports all listed argument forms, with the same editing scope as X.Interpolate |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)`                                                           |    √    |       △       |      √      |         ✕         | Supports all listed argument forms; edit existing distances and values or delete statements, preserving the argument count and key |
| `Track[trackKey].Cant.SetGauge(gauge)` / `[legacy] Track[trackKey].Gauge(gauge)`                                                                                                            |    √    |       △       |      √      |         ✕         | New statements use Cant.SetGauge; edit values and distances or delete either form, preserving the method and key |
| `Track[trackKey].Cant.SetCenter(x)`                                                                                                                                                         |    √    |       △       |      √      |         ✕         | Edit values and distances or delete statements, preserving the method and key |
| `Track[trackKey].Cant.SetFunction(id)`                                                                                                                                                      |    √    |       △       |      √      |         ✕         | New statements require `id` to be `0` or `1`; edit values and distances or delete statements, preserving the method and key |
| `Track[trackKey].Cant.BeginTransition()`                                                                                                                                                    |    √    |       △       |      √      |         ✕         | Edit distances or delete statements, preserving the method and key |
| `Track[trackKey].Cant.Begin(cant)`                                                                                                                                                          |    √    |       △       |      √      |         ✕         | Edit values and distances or delete statements, preserving the method and key |
| `Track[trackKey].Cant.End()`                                                                                                                                                                |    √    |       △       |      √      |         ✕         | Edit distances or delete statements, preserving the method and key |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / `[legacy] Track[trackKey].Cant(cant)`                                                                     |    √    |       △       |      √      |         ✕         | New statements use the 0/1-argument Interpolate forms; edit existing values and distances or delete statements, preserving the form and key |
| `Structure.Load(filePath)`                                                                                                                                                                  |    √    |       △       |      ✕      |         -         | Replace the list path; edit, add, delete, or reorder key/path rows in the table. Supports list version 1.00+ |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                                                                                    |    √    |       √       |      √      |         △         | All fields are editable; 3D gizmos adjust X/Y/Z |
| `Structure[structureKey].Put0(trackKey, tilt, span)`                                                                                                                                        |    √    |       √       |      √      |         △         | Properties/Edit converts between Put0 and Put; the Put0 Z-axis gizmo adjusts distance in whole meters |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)`                                                               |    √    |       √       |      √      |         △         | Supports both argument forms; property drafts update the 3D shape live, and the Z-axis gizmo adjusts distance in whole meters |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` |    √    |       △       |      √      |         △         | Create Begin/Begin0 alone or paired with End; supports change points, whole-segment renaming, linked deletion, form conversion, and position gizmos. Paired intervals with the same key must not overlap |
| `Repeater[repeaterKey].End()`                                                                                                                                                               |    √    |       △       |      √      |         △         | Create alone, with Begin/Begin0, or to close an open interval; supports distance editing and linked deletion. An additional End inside a closed interval with the same key is rejected |
| `Background.Change(structureKey)`                                                                                                                                                           |    √    |       √       |      √      |         ✕         | Edit distance and key; preview the background in the scene |
| `Station.Load(filePath)`                                                                                                                                                                    |    √    |       △       |      ✕      |         -         | Replace the list path; edit, add, delete, or reorder station rows in the table. Supports list version 0.04+. When stations reference sounds, Sound.Load/Station.Load order is checked and warnings are reported |
| `Station[stationKey].Put(door, margin1, margin2)`                                                                                                                                           |    √    |       √       |      √      |         ✕         | Edit distance, key, door side, and stop tolerances; requires `margin1 < 0` and `margin2 > 0` |
| `Section.Begin(...)` / `[legacy] Section.BeginNew(...)`                                                                                                                                     |    √    |       √       |      √      |         ✕         | Edit distance and the signal-index list; markers appear in 2D/3D |
| `Section.SetSpeedLimit(...)` / `[legacy] Signal.SpeedLimit(...)`                                                                                                                            |    √    |       √       |      √      |         ✕         | Edit distance and the speed-limit list; effective values appear in the 3D signal summary |
| `Signal.Load(filePath)`                                                                                                                                                                     |    √    |       △       |      ✕      |         -         | Edit aspect rows, glare rows, and trailing columns; main/glare rows are handled as a group, with glare added manually. Displays up to 509 structure-key columns |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                  |    √    |       √       |      √      |         △         | Both forms are editable; new statements use the full form. Extending a short form requires conversion confirmation; 3D gizmos adjust X/Y/Z |
| `Beacon.Put(type, section, sendData)`                                                                                                                                                       |    √    |       √       |      √      |         ✕         | Edit distance and all parameters |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()`                                                                                                                                                  |    √    |       √       |      √      |         ✕         | Edit, create, and delete Begin/End independently |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)` | √ | √ | √ | ✕ | Edit distance and passing time or delete through 2D/3D markers; create in the wizard's Signal category. Enter time as hh:mm:ss or seconds; hover over `passTime` for format help |
| `Light.Ambient(...)`, `Light.Diffuse(...)`, `Light.Direction(...)`                                                                                                                          |    √    |       √       |      √      |         ✕         | View and edit parameters in Light Sources; create at distance `0` in the wizard's Effects category. RGB range is `[0, 1]`, Direction requires distance `0`, and each statement type must be unique across the root map and Includes |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / `[legacy] Fog.Set(density, red, green, blue)`                                             |    √    |       √       |      √      |         ✕         | Supports 0/1/4-argument Interpolate and legacy Set; previews exponential fog and its transitions in 3D |
| `[compatibility] Legacy.Fog(start, end, red, green, blue)`                                                                                                                                   |    √    |       √       |      √      |         ✕         | Edit through tables or 2D/3D markers; previews linear fog and its transitions in 3D, including mixed use with Fog |
| `DrawDistance.Change(value)`                                                                                                                                                                |    √    |       √       |      √      |         ✕         | Edit distance and value; can control scene draw distance |
| `CabIlluminance.Interpolate(value)` / `CabIlluminance.Interpolate()` / `[legacy] CabIlluminance.Set(value)`                                                                                   |    √    |       √       |      √      |         ✕         | Edit distance and illuminance; a blank value writes Interpolate(). Tables and 3D boards show the previous effective value, or remain blank if none exists |
| `Irregularity.Change(x, y, r, lx, ly, lr)`                                                                                                                                                  |    √    |       √       |      √      |         ✕         | View data in tables and markers; edit distance and all six parameters |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)`                                                                                                                                           |    √    |       √       |      √      |         ✕         | View data in tables and markers; supports 1/3-argument forms |
| `Sound.Load(filePath)`                                                                                                                                                                      |    √    |       △       |      ✕      |         -         | Replace the list path; edit, add, delete, or reorder sound rows in the table. Supports list version 2.00+ |
| `Sound[soundKey].Play()`                                                                                                                                                                    |    √    |       √       |      √      |         ✕         | View sound events in tables and markers; edit distance and key |
| `Sound3D.Load(filePath)`                                                                                                                                                                    |    √    |       △       |      ✕      |         -         | Replace the list path; edit, add, delete, or reorder 3D sound rows in the table. Supports list version 2.00+ |
| `Sound3D[soundKey].Put(x, y)`                                                                                                                                                               |    √    |       √       |      √      |         △         | Edit distance, key, and X/Y; 3D boards mark sound-source positions, and gizmos adjust X/Y offsets and change distance in whole meters |
| `RollingNoise.Change(index)`                                                                                                                                                                |    √    |       √       |      √      |         ✕         | View noise events in tables and markers; edit distance and index |
| `FlangeNoise.Change(index)`                                                                                                                                                                 |    √    |       √       |      √      |         ✕         | View noise events in tables and markers; edit distance and index |
| `JointNoise.Play(index)`                                                                                                                                                                    |    √    |       √       |      √      |         ✕         | View noise events in tables and markers; edit distance and index |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)`                                                                                |    △    |       ✕       |      ✕      |         ✕         | Displays other-train definitions with partial reading of external definition files |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)`                                                                                                                           |    √    |       ✕       |      ✕      |         ✕         | Displays the enable time above the other-train stop-position table |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)`                                                                                                                             |    √    |       ✕       |      ✕      |         ✕         | Displays read-only other-train stop-position tables, paths, and map markers |

## Installation and Startup

Follow the [developer guide](docs/dev.md) to build the application, then run the generated executable.

After a Release build, run `build_release\komapedit.exe`. The application loads `maploader.dll`, `model_loader.dll`, and their dependencies from the adjacent `bin` directory. Debug builds use the corresponding `build` directory.

On startup, the application creates the `settings` directory as needed and creates or reads:

- `settings/imgui.ini`: UI window positions and layout.
- `settings/settings.ini`: UI language, font/component/station-marker sizes, 2D line widths, theme color, edit-mode warning state, and 3D settings such as automatic scene loading when opening a map, fog, draw distance, gizmo size, camera speed, and performance warnings.
- `settings/history.ini`: Recent maps, background-image alignment parameters, and per-map Custom Message display preferences.

Use the interface to change settings. The application reads valid entries in the current settings format and uses defaults for other entries; saving writes a complete settings file.

Use the executable and DLLs from the same build. If a build script reports old INI or DLL files in the output root, follow its instructions to arrange them under `bin`/`settings`, then build again.

## Usage

This section covers the interface, previews, and editing. You can use the views directly to inspect a map; before changing files, read section 10 and back up the map.

### 1. Main Window

The main window contains these areas:

- **Top menu**: Opens files, shows or hides windows, changes 2D/3D settings, and opens help pages.
- **Toolbar**: Provides common actions such as Open, Reload, editing, Save, Station Jump, and mileage jump.
- **Central workspace**: Shows the 2D View, File Structure Diagram, Text Preview, Structure Model Preview, and 3D Scene Preview. Windows can be stacked as tabs or dragged to other docking positions.
- **Map information tables**: Usually docked on the right. They show stations, structures, signals, sounds, and other map elements.
- **Console window**: Usually docked at the lower right. It shows detailed messages from map loading, model loading, editing, and saving.
- **Bottom status bar**: Shows the current operation and the number of errors and warnings.

If you close a window, reopen it from `Map Info List`, `2D View`, `3D View`, or `Auxiliary Info`. Window visibility and docking layout are saved automatically.

### 2. Top Menu

#### File

- `New...`: Opens the New File Wizard. It can create a map, Scenario, or resource-list file. See section 10 for details.
- `Open...`: Selects a `.txt` or `.csv` map or scenario file. The toolbar `Open` button does the same thing.
- `Recent Maps`: Opens a recently used map or clears the history.
- `Reload`: Reads the current map again and reloads the current Structure Model Preview. The shortcut is `F5`. If there are unsaved changes, the program asks for confirmation first.
- `Export CSV...`: Selects a directory and exports geometry data for the own track and every other track. Filename conflicts stop the export; check the Console for conflicts or write errors. See the appendix for the CSV fields.
- `Exit`: Closes the program. If there are unsaved changes, you can save them, discard them, or cancel the exit.

Opening a `BveTs Scenario 2.00` file lets you view its Scenario data and load a map from it:

- `Map Info List -> Other -> Scenario File` shows Scenario fields, Route/Vehicle paths, and weights. Enable editing to modify existing fields. Right-click a path to select a file or open its directory; Route/Vehicle candidates can also be added, deleted, or moved up and down. New candidates start with an empty path and weight `1`. Each existing Route/Vehicle field must retain at least one candidate.
- If there is one valid Route, its map loads directly. If there are several candidates, you choose one.
- If Route is missing or invalid, or you cancel the choice, the Scenario preview stays available. Map loading depends on the selected Route; Vehicle data is used for preview, and candidate weights are validated.
- Toolbar `Save` or `Ctrl+Shift+S` writes Scenario changes directly to its file. When both the map and Scenario have changes, the map is saved first. If the Route path has also changed, the program prompts you to save the Scenario again; reloading then switches the map.
- Recent files, background alignment history, and reloads use the Scenario file as the entry point. Reload rereads the Scenario, selects a Route when needed, and refreshes models. `Reload Track Geometry` keeps scene models and the camera where possible.

#### Options

- `UI Settings...`: Changes text size, UI component size, and the interface theme color.
- `2D Canvas Settings -> Canvas Element Sizes`: Changes marker sizes and line widths for the own track, other tracks, chart guides, and grid.
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
- Under Track Geometry, `Gauge Setting Points`, `Cant Center Setting Points`, and `Transition Function Setting Points` control white rectangular `CG`, `CC`, and `CF` markers in the plan and matching white two-line boards in 3D. These three switches are independent and off by default.
- `Section Markers` is off by default. When enabled, green `S` markers and their signal-index parameters appear in 2D and 3D.
- `Own Track Markers` and `Show Current Position on Plan` control helper displays shared by the 3D scene and 2D plan.
- `Other -> Custom Messages`, `File Structure Diagram`, `Text Preview`, and `Console` open the corresponding tool windows.

Signal markers are also controlled by the `Show` checkbox in each `Ground Signal List` row. Other-train paths are controlled one at a time in the `Other Train List`.

#### Language and Help

- `Language`: Switches between Simplified Chinese, English, and Japanese.
- `Help`: Opens the online documentation, issue-reporting page, or About window.

### 3. Toolbar

- **Open**: Selects and opens a map or scenario file.
- **Reload**: Reads the map again and reloads the current single-model preview. It is the same as pressing `F5`.
- **Reload Track Geometry**: Reads the map geometry again while keeping already loaded 3D scene models where possible. Use it after changing only route code.
- **Enable Edit**: Turns editing on or off. A risk warning appears the first time you enable it.
- **Add Map Element**: Opens the New Map Element Wizard once editing is enabled and edit data has loaded.
- **Save**: Writes applied map changes and Scenario changes to their source files. The shortcut is `Ctrl+Shift+S`. Apply all resource-list drafts before saving.
- **Revert**: Discards all unsaved changes and restores the version on disk. The program asks for confirmation first.
- **Station Jump**: Selects a station and moves the 2D View to it. If the 3D scene is running, its camera also moves.
- **Jump to distance(m)**: Enter a number and click `Jump`, or press `Enter` in the input box. If the 3D scene is running, its camera also moves.

The global shortcuts are `F5` for Reload and `Ctrl+Shift+S` for Save. See sections 5 and 9 for 2D and 3D canvas controls.

### 4. Status Bar and Console

#### Bottom Status Bar

- `Err` and `Warn` on the left show the number of errors and warnings. Click this area to view a summary.
- The status text on the right shows states such as Loading, Ready, Applying changes, and Saved. After a long operation finishes, it also shows the elapsed time.

#### Console Window

The Console shows complete loading, parsing, model, and editing logs. Check it first when a map does not open, a model is missing, or saving fails.

Apply, Save, and Delete show their total execution time in the status bar. Per-stage timings appear in the Console's `edit timing:` records.

- **Clear**: Removes all log messages and resets the error and warning counts.
- **Copy**: Copies the full log to the clipboard for use in a bug report.
- The log follows new messages while at the bottom; scroll up to stay on earlier messages.

### 5. 2D Canvas

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

- Use `Auxiliary Info` to choose which markers to show.
- With `Curve Radius` enabled, `Curve.Interpolate` endpoints appear as green markers with their evaluated signed radius beside them. An omitted radius displays its inherited value.
- Right-click a marker to locate its table row. Items with a 3D object can also be located in the 3D scene.
- Enable editing to right-click supported markers for `Properties/Edit` or `Delete`. Own-track curve/gradient and other-track edit control points appear in this mode; `Curve.Interpolate` and `CG`/`CC`/`CF` markers follow their respective visibility switches.
- Paired curve or gradient change points have an edit menu in the Profile and Curve Radius charts.

#### Background Image

After importing an image from `2D View -> Background Image`, you can show it, adjust it manually, or choose `Align Background to Stations`. For station alignment, select two stations and double-click their matching positions on the image. The program calculates the image position, scale, and rotation.

### 6. Map Information Tables

#### Common Actions

- Open the required table from `Map Info List`. Right-click a mileage cell that has a locate menu to move to that position in the Plan or a running 3D scene.
- Right-click a source-file or resource-file path to open its directory. Hover over a path to see the original argument and resolved absolute path when available.
- The Structure List, Signal Aspects List, Sound File List, and 3D Sound File List tables support partial or exact searches, previous and next results, and searches for unused entries.
- Unused Sound File searches include station arrival/departure sounds and use the current station-list draft to determine references.
- In a map-placement row, right-click a resource key to jump to the matching Structure List, Signal Aspects List, or sound definition.
- With editing enabled, right-click an editable element's cell for `Properties/Edit` or `Delete`. Some tables also open `Properties/Edit` on double-click.

#### Stations and Structures

- **Station List**: The upper part shows `Station.Put` stop positions. The lower part shows station definitions loaded by `Station.Load`, including fields such as station name, stop time, and door side.
- **Map Structure List**: Shows `Structure.Put` and `Structure.Put0`. You can locate an item in the Plan, the 3D scene, or its model definition.
- **Map Structure List (PutBetween)**: Shows `Structure.PutBetween` structures deformed between two tracks.
- **Structure List**: Shows each structureKey and model path. Right-click a key to preview the model or fill that key into an open New Map Element Wizard of a matching type.
- **Repeater List**: Groups `Repeater.Begin`/`Begin0`/`End` statements with the same key into intervals, with jumps to start, end, and change points. Multi-segment intervals support `Delete All`, `Delete Change Point`, `Trim to Change Point`, and `Start from Change Point`. Each Begin cycles through models at regular intervals from its own start, ending before End or the next Begin with the same key. Events at the same mileage follow source order. Missing models leave gaps in the cycle; zero-length segments show only editing information.
- **Other Train List**: Shows other-train definitions, stop positions, and the read-only `Train.Enable` time. You can control each path separately and locate stop positions in the Plan.

#### Track Geometry, Signals, and Sections

- **Other Tracks**: Controls each track's visibility, range, and color. With editing enabled, right-click `Key` to rename matching `Track[...]` statements; update track references in Structure, Signal, and Repeater statements separately.
- **Track Irregularity, Adhesion Change Point, and Speed Limit Point lists**: Show the corresponding positions and can locate them in 2D or 3D. Speed-limit Begin and End may exist independently; End edits only its mileage.
- **Signal Aspects List**: Shows signal aspect definitions and their structure keys. You can jump from a structure key to its model.
- **Ground Signal List**: Shows `Signal.Put` positions and parameters. Each row's `Show` checkbox controls its Plan marker.
- **Section List**: Shows `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit`, with variable-length parameters and explicit `null` values. Each row displays up to 508 parameters; view the full set in `Properties/Edit`.
- **Beacon List**: Shows and locates `Beacon.Put` statements.
- **Variable List**: Groups all assignments by case-insensitive variable name. Hover to see the original expression. This table is read-only.

#### Sounds and Effects

- **Sound File List and 3D Sound File List**: Show sound keys, file paths, and buffer counts. You can open a file's directory or fill its key into a matching New Map Element Wizard.
- **Sound Playback Point List, Fixed Sound Source List, Rolling Noise Change Point List, Flange Noise Change Point List, and Joint Noise Play Point List**: Show and locate playback or change positions.
- **Background Change Point List, Cab Illuminance Change Point List, Fog Change Point List, Legacy Fog Change Point List, and Scenery Draw Distance Change Point List**: Show and locate the corresponding effects.
- **Scenario File**: Shows the open Scenario's fields and candidates. See section 2 for editing and saving.

#### Inline Editing in Resource Lists

Station definitions, Structure List, Signal Aspects List, Sound File List, and 3D Sound File List use the same inline editing controls:

- Double-click an editable cell to type, then press `Enter` to finish that cell. Right-click to select a file, insert a row above or below, move the whole row up or down, clear the cell, or delete the whole row.
- Use `Add Row` to add the first entry to an empty list.
- `Select File` stores a relative path where possible and an absolute path otherwise.
- A signal aspect's primary and glare rows move or are deleted as a block. A new primary row starts with six fields; use `Add Glare` to add a glare row.
- Click the table's `Apply` to update the in-memory preview. After applying all table drafts, click toolbar `Save` to write the files.

The Signal Aspects List uses each CSV row's actual field count, including trailing empty fields. A diagonal line marks a field absent from that row; existing empty fields are editable. The table displays up to 509 structure-key columns, with a notice for additional fields retained in the source and draft.

Multiple consecutive glare rows for one aspect appear as a combined row. You can edit values or move the block; `Delete Glare` removes all its glare rows. Keep their total glare field count unchanged, as changing it causes an error on `Apply`. An aspect usually has one glare row and supports the column operations below directly.

- Right-click a CSV cell to append a cell at the right, trim trailing empty cells, or remove the last cell in that row. Primary and glare rows can have different field counts.
- `Align All Columns` pads each row to the maximum field count. `Add Column on Right` and `Delete Rightmost Column` add or remove one cell per row. `Delete All Trailing Empty Cells` trims each row's trailing empty fields.
- Each row retains its first CSV field and at least one structure-key field, which may be empty. A glare row's first field is fixed as empty; use `Delete Glare` to remove the row.
- Deleting a nonempty last cell requires confirmation; a batch deletion asks once.
- Column operations include fields beyond the display limit. Submit changes with `Apply`, then `Save`.

A map can load one resource list of each type. To add a missing list, click `New or Import File` in the table. To replace it, right-click `Source path` at the top and choose `Change File...`. If that list has unapplied or unsaved changes, the program asks for confirmation before discarding that list's changes.

Importing or replacing a resource list checks its header and version: Station List supports 0.04+, Structure List 1.00+, and Signal Aspects List and the Sound Lists used for ordinary and 3D sounds 2.00+. Comments can use `#` or `//` outside CSV double quotes; quoted `//` is treated as text.

List uniqueness is checked across the entry map and its Includes. `Train[].Enable` keys must also be unique, ignoring case. Duplicate definitions cause loading to fail; see the Console for details.

### 7. File Structure Diagram

Each Map file starts with its own `distance` at `0`; returning from an Include restores the parent's mileage. Ordinary `$variables` are shared. To position a submap relative to the parent, assign `$dis=distance;` before the Include and use `$dis+offset;` inside it. Structure coordinates and rotation parameters also support variables and expressions.

Open this window from `Auxiliary Info -> Other -> File Structure Diagram`. The entry map is on the left, and its included submaps are shown by level to the right. Hover over a node to see the Include argument and absolute path.

#### Viewing Files

- Right-click a valid node and choose `Preview Text` to view its source and line numbers in the read-only Text Preview.
- `Open in File Explorer` opens the target directory; a missing directory is reported in the Console.
- Missing or invalid Include targets are red. Text preview, submap import, and new-submap actions are disabled for them.

#### Editing Includes

The following actions require editing to be enabled:

- **Change Included File...**: Selects a new `.txt` or `.csv` submap and rewrites the Include path in its parent file. A relative path is preferred; an absolute path is used if needed.
- **Unlink Include**: Deletes the Include statement from its parent. The action is blocked if later statements still depend on data from that submap.
- **Import Submap...**: Selects an existing BVE map and stages a new Include in the source file represented by the current node.
- **New Submap...**: Creates a blank submap at a new file path with the `BveTs Map 2.02:utf-8` header, UTF-8 without BOM, and CRLF line endings, then stages its Include.

A new Include goes after the last Include preceding the first local distance statement. With no preceding Include, it goes before the first distance statement; with neither, it is appended to the file. Changes update the preview immediately and are written to the parent map on `Save`.

### 8. 3D Model Preview

Open `3D View -> Structure Model Preview`, then right-click a structureKey in the Structure List and choose `Preview Model`.

- **Structure List**: Opens the Structure List.
- **Reload**: Reads the current model, materials, and textures again.
- **Clear**: Removes the current model from the preview.
- **Background Color**: Selects any RGB color or uses the white, black, gray, blue, or green shortcut colors.
- Drag with the left mouse button to rotate the model. Scroll the mouse wheel to zoom.

Check the Console for details of model or texture loading failures.

### 9. 3D Scene Preview

Open `3D View -> 3D Scene Preview` and click `Start 3D Scene Preview`. To start it whenever a map opens or reloads, enable `Automatically load scene preview when opening a map` in `Options -> 3D Canvas Settings` (off by default).

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

Set the camera speed in `3D Canvas Settings`. Use the toolbar to jump to a station or mileage. The camera can move up to 100 m behind the own-track start. Enable `Show Current Position on Plan` to show its position in the 2D Plan.

#### Select Mode

- Move the pointer over a scene object or marker to highlight it.
- Right-click an object or marker to locate its Map Info table. For a Repeater, you can also jump to the start or end/change position.
- With editing enabled, the same menu also provides `Properties/Edit` and delete actions. See section 10 for edit gizmos.

#### Mileage Select Mode

- The nearest whole-meter cross-section on the own-track plane and its mileage label appear near the pointer.
- With editing enabled, right-click this position and choose `Add Map Element at Current Mileage`. The wizard fills in `distance` automatically.

#### Scene Overlay and Settings

The canvas shows the camera position, current curve radius and cant, gradient, speed limit, section signal speed, and next-station information. A `Curve.Interpolate` interval shows both endpoints' radii, cants, and curve directions; when both radii are zero, it shows `Straight`. The bottom shows scene chunks, instances, loaded models, and frame rate.

FPS is the scene canvas's average render-call rate over the latest `0.2`-second active window; the last value is kept while idle.

`3D Canvas Settings` can immediately toggle fog, map-driven draw distance, and performance warnings, and can change the normal draw distance. Related marker visibility stays synchronized with `Auxiliary Info`.

Fog supports exponential `Fog.Interpolate`/`Fog.Set` and linear `Legacy.Fog`. Legacy start/end values are camera-depth distances in meters, and RGB uses the 0–255 scale. Legacy settings at mileage zero take effect immediately; later settings transition over 25 meters when both states are linear. Fog interpolates between nodes of the same type; different types switch at the later node.

With editing enabled, edit or delete `Legacy.Fog` through its list or 2D/3D markers, or add it from the New Map Element Wizard's Effects category.

#### Scene Boards

Use `Auxiliary Info` to control board visibility. With `Curve Radius` enabled, `Curve.Interpolate` points show radius, cant, and curve direction; a zero radius shows `Intpl. 0`. Boards support picking and highlighting, plus right-click editing and deletion when editing is enabled.

### 10. Editing, Creating, and Saving

#### Before Editing

The first time you turn on `Enable Edit` on the toolbar, a prompt appears. You can select `Don't show again` and confirm.

When you disable editing, open another document, reload, or exit, the program prompts you to handle any unsaved changes.

#### Drafts, Apply, Save, and Revert

Map and resource-list editing follows these steps:

1. **Edit the draft**: Enter values in `Properties/Edit`, a resource-list table, or a wizard. Some objects provide a live preview.
2. **Apply to preview**: Click `Apply` in the window to validate and reparse the in-memory working copy, then refresh the 2D View, tables, and 3D preview.
3. **Save to disk**: Click toolbar `Save` or press `Ctrl+Shift+S` to write all applied changes to the corresponding map, Include, or resource-list files.

Toolbar `Revert` discards all unsaved changes. `Reload` reads the files from disk again and asks for confirmation before discarding changes. Apply resource-list and Custom Messages drafts in their tables before saving.

Before saving, the program reparses and verifies the edited result, preserving the original encoding, BOM, and line endings. If the new text cannot be represented in the original encoding, or another program has changed a file, saving is blocked with an explanation.

#### Editing or Deleting Existing Map Elements

1. Right-click an item in a table or a 2D/3D marker and choose `Properties/Edit`.
2. Check the source file, source position, and raw statement in the window, then change the fields.
3. Click `Apply` to refresh the in-memory preview.
4. Check the 2D View, tables, and 3D result. If they are correct, use toolbar `Save`.

Editable items include stations, structures, signals, beacons, Repeaters, Sections, speed limits, curves and gradients, other tracks, track irregularity, sounds, backgrounds, adhesion, cab illuminance, fog, lighting parameters, draw distance, and PreTrain pass points. Choose `Delete` from the context menu to apply a deletion to the preview, then save it to the source file.

Use `Add Coordinate Offsets` or `Remove Coordinate Offsets` to switch between `Structure.Put`/`Put0` or `Repeater.Begin`/`Begin0`. Removing nonzero offsets requires confirmation. Editing Z, rotation, tilt, or span in a short-form `Signal.Put` requires confirmation to convert it to the full form.

#### Live Adjustment in the 3D Scene

Open `Properties/Edit` and drag a supported object's gizmo to adjust its draft, then apply and save as described above.

- `Structure.Put`, full-form `Signal.Put`, and `Repeater.Begin` use X/Y/Z axes to change coordinates.
- For `Sound3D[soundKey].Put(x, y)`, X/Y adjust the relative position in 0.001 m steps, and Z adjusts mileage in whole meters.
- `Structure.Put0` and `Repeater.Begin0` use Z to adjust mileage in whole meters. Adding coordinate offsets enables the full coordinate gizmo.
- A Repeater with an explicit End provides a Z axis at its end position to adjust the end mileage in whole meters.
- `Structure.PutBetween` uses a Z axis to change whole-meter mileage and recalculates the deformed model live.

#### New Map Element Wizard

Open the wizard from toolbar `Add Map Element`, or right-click the current mileage in 3D `Mileage Select` mode. Choose a target source file and template, then enter the parameters. Any loaded map source file, including a blank or distance-free file, can be a target.

When creating or moving an element, the program reuses a suitable distance block or creates one, preserving other statements and comments. An unambiguous mileage sequence in the final source section can extend beyond its last mileage. If the position or expression is ambiguous, follow the prompt to choose a highlighted insertion point in Text Preview or enter an explicit mileage expression. The edited values are validated before Apply succeeds.

The wizard provides the currently supported elements in these categories:

- **Structures**: `Structure.Put`, `Put0`, `PutBetween`, and `Repeater.Begin`/`Begin0`/`End`.
- **Station**: `Station.Put`.
- **Track Geometry**: Curves, gradients, gauge, curve center and interpolation function, track irregularity, and adhesion changes.
- **Other Tracks**: `Track.*` statements for position, X/Y interpolation, and cant.
- **Signal**: Signals, speed limits, Sections, signal speed, beacons, and `PreTrain.Pass` points.
- **Sound**: Map sounds, 3D sound sources, rolling noise, flange noise, and joint noise.
- **Effects**: Background, cab illuminance, fog, lighting parameters, and draw distance.
- **Other**: Custom Messages, stored as comments below the map file header.

`Apply` creates the element and refreshes the preview. `Apply and Edit` also closes the wizard and opens `Properties/Edit` for the new element, or the Custom Messages tab. Use `Apply` in the Repeater change-point wizard.

Template tips:

- **Repeater**: Add Begin, End, or both. When added as a pair, the end mileage must be at least the start mileage. Same-name intervals must avoid overlap. To add a change within an interval, use `Properties/Edit -> Insert Change Point`; the wizard prefills the current parameters and start mileage.
- **Curves and gradients**: The curve template provides `Curve.Begin`, `Curve.Change`, and `Curve.End`; the gradient template provides `Gradient.Begin` and `Gradient.End`. Both can create the corresponding transition starts.
- **Curve interpolation points**: `Curve.Interpolate` requires distance; radius and cant both default to `0`. Uncheck cant for the one-argument form, or radius for the zero-argument form.
- **Other tracks**: Enable optional trailing arguments in order; for example, include `radiusH` before `radiusV`. A new trackKey creates another track. Numeric keys and quoted string keys are treated separately.

#### Custom Messages (Message from Creator)

Choose `Other -> Custom Messages` in the New Map Element Wizard, select a map source file, and enter a single-line message. After applying and saving, the message is written after the blank line below the file header in this comment format, or appended to an existing message block:

```plaintext
BveTs Map 2.02

//--kme--message-from-creator:"content"
```

Messages are stored as ordinary BVE `//` comments. Contents are literal, preserving spaces, quotes, and backslashes. Empty messages are allowed; content must be a single line without NUL characters.

Opening a map or Scenario displays messages from the root map and its loaded Includes, with paging for multiple messages. Repeated references to the same file show each message once. Select `Do not show again` and confirm to remember that choice for the root map.

Open `Auxiliary Info -> Other -> Custom Messages` at any time to view the source file, line number, and content. With editing enabled, double-click a content cell to edit it; Enter or leaving the cell stages the draft, while Esc cancels the current input. Choose `Delete Entire Row` from the cell's context menu to stage deletion. Click the tab's `Apply`, then toolbar `Save`.

#### New File Wizard

Open `File -> New...` to create a Map, Scenario, Structure List, Signal Aspects List, Sound List, Sound List for Sound3D, or Station List.

1. Choose the file type, enter a file name, choose `.txt` or `.csv`, and select a directory.
2. For a resource list, use `Import File` to prefill the name, directory, and suffix from an existing `.txt` or `.csv`, then adjust them as needed.
3. For a Scenario, fill in the desired fields; empty fields are omitted. Use `Select File` to fill `Route`, `Vehicle`, or `Image` with a relative path.
4. To reference the file from the current map, enable editing and choose a loaded map source under `Reference in`.
5. Click `Confirm` to create or reuse the file. Maps and Scenarios also offer `Confirm and Load`.

The selected suffix is appended to the file name. Existing regular files are reused as they are. New files use UTF-8 and CRLF: maps and resource lists contain the standard header, while Scenarios write their contents in standard field order.

The Scenario template provides eight fields: `Title`, `Route`, `RouteTitle`, `Vehicle`, `VehicleTitle`, `Author`, `Image`, and `Comment`. Each path field accepts one path, and field values must avoid comment characters such as `#` and `;`. Edit multiple path candidates and weights in the Scenario File tab after loading.

The wizard creates the file immediately. Its `include` or corresponding `*.Load` reference is applied to the map preview and written by toolbar `Save`. A map can reference one resource list of each type. To replace a referenced file, use the `Source path` context menu at the top of its table.

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

CSV export contains track geometry, with numeric values formatted to six decimal places.

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
