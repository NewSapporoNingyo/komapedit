# komapedit User Manual

This manual describes komapedit's interface, preview and editing operations, and the formats of exported CSV data.

For a project overview, supported BVE map syntax, and installation and startup instructions, see the [README](../README.md).

## Reporting Bugs

Keep in mind: komapedit is a personal project consisting of *more than 80% vibeslop*, so you shouldn't expect it to be as stable as large-scale commercial software.

If you encounter crashes, display problems, differences between map element behavior in komapedit and BVE, or failures when editing or saving, use `Help -> Report Bugs` to open the issue submission page, or visit the project's [GitHub Issues](https://github.com/NewSapporoNingyo/komapedit/issues).

Before submitting a report, search for an existing issue about the same problem and add your information there if one exists. For a new problem, sign in to GitHub and create an issue. Use a title that briefly identifies the affected feature and symptom, such as "Crash when loading the 3D Scene Preview after opening a particular map."

**You should only open an issue when you encounter a genuine exception in the program. Please do not create issues such as “I don’t understand how to use a certain feature” or “I want the developers to add a new feature.”**

### Information to Provide

- **Operating system and hardware**: Your Windows version, OS build number, and system architecture. Press `Win+R` and enter `winver` to check the Windows version and build number. For 3D display or performance problems, also include your CPU, RAM capacity, graphics card model, and graphics driver version.
- **Program version**: The komapedit release version or Git commit, and where you obtained the program. If you built it yourself, also include the Debug/Release build type and compiler version. If you cannot identify the version, give the download or build date.
- **Observed and expected behavior**: Explain what you expected the program to do and what actually happened, preserving the complete text of any error messages. For slowdowns or freezes, describe which stage of the operation was affected and roughly how long it lasted.
- **Steps to reproduce**: List your actions in order, starting with launching the program and opening the file. Include relevant menus, parameters, settings, and whether you clicked `Apply` or `Save`. State whether the problem occurs every time or only occasionally. If it affects only a particular position or element, include the mileage, element key, or source-file location.
- **Console log**: Include the complete log from loading the relevant files through the point after the problem occurs. See below for how to obtain it. Long logs can be attached as `.txt` files.
- **Reproduction files**: If the problem occurs only on a particular map, provide map files that reproduce it where possible.<br>
For a map that is not publicly available, provide the smallest set of files that reproduces the problem where possible; the complete map is not necessarily required. You can email the necessary files to the developer, such as the map, included submaps, resource lists, models, or textures. Preserve their relative directory structure.<br>
For a publicly available BVE map, provide its download link so the project developer can reproduce the problem.<br>
For editing or saving problems, also include the files before and after the change if available, and identify the differences.
- **Screenshots or recordings**: For interface, 2D/3D display, or operation-sequence problems, attach screenshots or a short recording that shows the issue. Include the written description and logs as well so the report can be searched and analyzed.

Before uploading, check logs, screenshots, and attachments for information such as personal paths. If you provide everything directly in GitHub Issues, use reproduction files that can be shared publicly, while preserving the conditions needed to trigger the problem wherever possible.

### Obtaining the Console Log

1. Open the Console from `Auxiliary Info -> Console Window`.
2. If the problem can be reproduced, repeat the steps using test files or copies, keeping the log from file loading through the point after the problem occurs. Do not click `Clear` before copying it.
3. Click `Copy` in the Console to copy the entire log to the clipboard.
4. Paste the log into a code block in the issue, or save it as a UTF-8 text file and upload it as an attachment.

If the program has crashed, is unresponsive, or cannot start, and you cannot copy the Console log, state this in the report and provide the last action you performed, any error dialogs, or screenshots. If related application errors are available in Windows Event Viewer, you can include them as well.

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
- `Reload`: Reads the current map again, refreshes an active 3D scene, and reloads the current single-model preview. The shortcut is `F5`. If there are unsaved changes, the program asks for confirmation first.
- `Export CSV...`: Selects a directory and exports the current geometry data for the own track and every other track, overwriting existing files with the same names. Export stops if different tracks generate the same output filename; see the Console for details. See the appendix for the CSV fields.
- `Exit`: Closes the program. If there are unsaved changes, you can save them, discard them, or cancel the exit.

Opening a `BveTs Scenario 2.00` file lets you view its Scenario data and load a map from it:

- Open `Scenario File` from the Other group in `Map Info List` to view fields, Route/Vehicle paths, and weights. Enable editing to change values directly in the input boxes. Text fields such as the title, author, and comment, as well as the Image path, can also be filled in if absent.
- Right-click a path to select a file or open its directory. Existing Route/Vehicle fields support adding, deleting, and reordering candidates, with at least one candidate per field. New candidates default to weight `1` and require a path; weights must be greater than `0`.
- If there is one valid Route, its map loads directly. If there are several candidates, you choose one.
- If Route is missing or invalid, or you cancel the choice, you can still view and edit the Scenario. Vehicle entries show their referenced paths and weights.
- Toolbar `Save` or `Ctrl+Shift+S` writes Scenario changes directly to its file. When both the map and Scenario have changes, the map is saved first. If Route candidate paths or their order have also changed, the program prompts you to save the Scenario again. After saving, use `Reload` to load the selected map.
- Recent files, background alignment history, and reloads use the Scenario file as the entry point. Reload rereads the Scenario, selects a Route when needed, and refreshes models. `Reload Track Geometry` keeps scene models and the camera where possible.

#### Options

- `UI Settings...`: Changes text size, UI component size, and the interface theme color.
- `2D Canvas Settings -> Canvas Element Sizes`: Changes marker sizes and line widths for the own track, other tracks, chart guides, and grid.
- `2D Canvas Settings -> Plot Range...`: Enter the minimum and maximum mileage and click `Apply` to change the 2D display range. `Reset` fills in the default range; click `Apply` to use it.
- `2D Canvas Settings -> Control Points...`: Changes the sampling range and interval for track geometry. Click `Apply` to regenerate the geometry data. A smaller interval gives denser curve samples; CSV export uses the regenerated data.
- `3D Canvas Settings`: Changes scene draw distance, edit component size, camera speed, fog, map-driven draw distance, automatic loading, and performance warnings.

Text and component sizes, colors, and 3D display parameters support live preview. Click `OK` to save settings or `Cancel` to restore the values from before the dialog opened.

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
- `Gauge Setting Points`, `Cant Center Setting Points`, and `Transition Function Setting Points` show `CG`, `CC`, and `CF` markers respectively. `Section Markers` shows green `S` markers and signal indices. All four switches are off by default.
- `PreTrain Pass Points` shows `PreTrain.Pass` markers. With editing enabled, right-click to change their mileage or pass time, or delete them.
- `Own Track Markers` and `Show Current Position on Plan` control helper displays shared by the 3D scene and 2D plan.
- `Other -> Custom Messages`, `File Structure Diagram`, `Text Preview`, and `Console Window` open the corresponding tool windows.

Signal markers in the Plan are controlled by the `Show` checkbox in each `Ground Signal List` row. Other-train paths are controlled one at a time in the `Other Train List`.

#### Language and Help

- `Language`: Under `中文`, select `简体` for Simplified Chinese, `台湾繁體` for Traditional Chinese (Taiwan), or `香港繁體` for Traditional Chinese (Hong Kong). `English` and `日本語` remain beside the `中文` submenu. A checkmark identifies the active language; the selection is saved for the next launch.
- `Help`: Opens the online documentation, issue-reporting page, or About window.

### 3. Toolbar

- **Open**: Selects and opens a map or scenario file.
- **Reload**: Reads the map again, refreshes an active 3D scene, and reloads the current single-model preview. It is the same as pressing `F5`.
- **Reload Track Geometry**: Reads the map again and updates geometry and placements, reusing loaded 3D scene models and preserving the camera position where possible. Use it to refresh the preview after editing route code externally.
- **Enable Edit**: Turns editing on or off. A risk warning appears the first time you enable it.
- **Add Map Element**: Opens the New Map Element Wizard once editing is enabled and edit data has loaded.
- **Save**: Writes applied map changes and Scenario changes to their source files. The shortcut is `Ctrl+Shift+S`. Apply resource-list and Custom Messages drafts in their respective tables first.
- **Revert**: After confirmation, discards all pending changes, resource-list and Custom Messages drafts, and Scenario changes, restoring the in-memory working copy to its state at the last save.
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

At the top of the 2D View, choose `Move` or `Measure` mode. A `Fixed` grid stays in place on screen; a `Movable` grid pans, zooms, and rotates with the map; `None` hides the grid. Drag the splitter between the Plan and the charts below up or down to adjust their heights. Drag the splitter between the two charts left or right to adjust their widths.

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

#### Profile and Curve Radius Charts

The Profile chart shows mileage and elevation. The Curve Radius chart distinguishes left and right curves and labels their radii. Drag with the left mouse button to pan, and use the mouse wheel to zoom the horizontal mileage range. In `Move` mode, double-click to fit the view. On a touchscreen, a horizontal two-finger pinch adjusts the mileage range; the Profile chart also supports a vertical pinch to zoom the elevation axis.

#### Plan and Charts: Measure Mode

- Move the pointer near the track to see its mileage, elevation, gradient, curve radius, and current speed limit.
- Double-click the Plan, Profile, or Curve Radius chart to move all three measurement positions to that mileage.

#### Markers, Navigation, and Editing

- Use `Auxiliary Info` to choose which markers to show.
- With `Curve Radius` enabled, `Curve.Interpolate` endpoints appear as green markers with their evaluated signed radius beside them. An omitted radius displays its inherited value.
- Right-click a marker to locate its table row. Items with a 3D object can also be located in the 3D scene.
- Enable editing to right-click supported markers for `Properties/Edit` or `Delete`. Own-track curve/gradient and other-track edit control points appear in this mode; `Curve.Interpolate` and `CG`/`CC`/`CF` markers follow their respective visibility switches.
- Paired curve or gradient change points have an edit menu in the Profile and Curve Radius charts.

#### Background Image

Click `Import` in the Background Image group of the `2D View` menu to select an image. `Show` controls its visibility, and `Adjust` sets its position, width, height, rotation, and brightness.

For a map with at least two stations, use `Align to Station`:

1. Select `Station 1`, click `Pick station on Plan`, and double-click that station's position on the background image.
2. Select a different station as `Station 2` and pick its position in the same way.
3. Click `Apply` or `OK` to calculate the image position, scale, and rotation.

The image path and alignment settings are saved for the current map or Scenario entry and restored when it is reopened.

### 6. Map Information Tables

#### Common Actions

- Open the required table from `Map Info List`. Right-click a mileage cell that has a locate menu to move to that position in the Plan or a running 3D scene.
- Right-click a source-file or resource-file path to open its directory. Hover over a path to see the original argument and resolved absolute path when available.
- In the Structure List, Signal Aspects List, Sound File List, and 3D Sound File List tables, expand `Find`, choose partial or exact matching, enter a query, and press `Enter` or click `Find`. Use the up and down arrows to move between results. English text matching is case-insensitive.
- `Search unused...` highlights definitions that the current map does not reference. Structure searches include placements, Repeaters, backgrounds, signals, and other-train references. Ordinary sound searches include station arrival/departure sounds and account for current list drafts.
- In a map-placement row, right-click a resource key to jump to the matching Structure List, Signal Aspects List, or sound definition.
- With editing enabled, right-click an editable element's cell for `Properties/Edit` or `Delete`. Some tables also open `Properties/Edit` on double-click.

#### Stations and Structures

- **Station List**: The upper part shows `Station.Put` stop mileage, door side, and stopping tolerances before and after the stop position; choose `Properties/Edit` from the context menu to change them. The lower part uses inline editing for definitions loaded by `Station.Load`, including station names, arrival/departure times, stop times, and sounds.
- **Map Structure List**: Shows `Structure.Put` and `Structure.Put0`. You can locate an item in the Plan, the 3D scene, or its model definition.
- **Map Structure List (PutBetween)**: Shows `Structure.PutBetween` structures deformed between two tracks.
- **Structure List**: Shows each structureKey and model path. Right-click a key to preview the model or fill that key into an open New Map Element Wizard of a matching type.
- **Repeater List**: Groups `Repeater.Begin`/`Begin0`/`End` statements with the same key into intervals, with jumps to start, end, and change points. Use `Properties/Edit` to adjust segment parameters, model order, and end mileage. See section 10 for details.
- **Other Train List**: Shows other-train definitions, stop positions, and the read-only `Train.Enable` time. You can control each path separately and locate stop positions in the Plan.

#### Track Geometry, Signals, and Sections

- **Other Tracks**: Use `Show` or `Select All` to control track visibility in 2D and 3D. Start/end mileage and color adjust the 2D view. With editing enabled, right-click `Key` to rename matching `Track[...]` statements; update track references in Structure, Signal, and Repeater statements separately.
- **Track Irregularity, Adhesion Change Point, and Speed Limit Point lists**: Show the corresponding positions and can locate them in 2D or 3D. Speed-limit Begin and End may exist independently; End edits only its mileage.
- **Signal Aspects List**: Shows signal aspect definitions and their structure keys. You can jump from a structure key to its model.
- **Ground Signal List**: Shows `Signal.Put` positions and parameters. Each row's `Show` checkbox controls its Plan marker.
- **Section List**: Shows signal indices from `Section.Begin`/`BeginNew` and aspect speed limits from `Section.SetSpeedLimit`/`Signal.SpeedLimit`. Use `+`, `-`, and the up/down arrows in `Properties/Edit` to add, remove, and reorder parameters. For aspect speed limits, `null` means no speed restriction. Each row displays up to 508 parameters; view the full list in `Properties/Edit`.
- **Beacon List**: Shows and locates `Beacon.Put` statements.
- **Variable List**: Groups all assignments by case-insensitive variable name. Hover to see the original expression. This table is read-only.

#### Sounds and Effects

- **Sound File List and 3D Sound File List**: Show sound keys, file paths, and buffer counts. You can open a file's directory or fill its key into a matching New Map Element Wizard.
- **Sound Playback Point List, Fixed Sound Source List, Rolling Noise Change Point List, Flange Noise Change Point List, and Joint Noise Play Point List**: Show and locate playback or change positions.
- **Background Change Point List, Cab Illuminance Change Point List, Fog Change Point List, Legacy Fog Change Point List, and Scenery Draw Distance Change Point List**: Show and locate the corresponding effects.
- **Light Sources**: Shows RGB values for `Light.Ambient` and `Light.Diffuse`, and pitch/yaw for `Light.Direction`. With editing enabled, change parameters directly and click this window's `Apply`, then toolbar `Save`. Use `New` to add a missing item or `Delete` to remove an existing one.
- **Scenario File**: Shows the open Scenario's fields and candidates. See section 2 for editing and saving.

#### Inline Editing in Resource Lists

With editing enabled, station definitions, Structure List, Signal Aspects List, Sound File List, and 3D Sound File List use these inline editing controls:

- Double-click an editable cell to type, then press `Enter` or leave the cell to stage the draft. Right-click to insert a row above or below, move the whole row up or down, clear the cell, or delete the whole row. Resource-file path cells also offer `Select File`.
- Use `Add Row` to add the first entry to an empty list.
- `Select File` stores a relative path where possible and an absolute path otherwise.
- A signal aspect's primary and glare rows move or are deleted as a block. A new primary row starts with six fields; use `Add Glare` to add a glare row.
- Click the table's `Apply` to update the in-memory preview. After applying all table drafts, click toolbar `Save` to write the files.

The Signal Aspects List uses each CSV row's actual field count, including trailing empty fields. A diagonal line marks a field absent from that row; existing empty fields are editable. The table displays up to 509 structure-key columns, with a notice for additional fields retained in the source and draft.

- Right-click a CSV cell to append a cell at the right, trim trailing empty cells, or remove the last cell in that row. Primary and glare rows can have different field counts.
- `Align All Columns` pads each row to the maximum field count. `Add Column on Right` and `Delete Rightmost Column` add or remove one cell per row. `Delete All Trailing Empty Cells` trims each row's trailing empty fields.
- Each row retains its first CSV field and at least one structure-key field, which may be empty. A glare row's first field is fixed as empty; use `Delete Glare` to remove the row.
- Deleting a nonempty last cell requires confirmation. Batch column operations also include fields beyond the display limit.

Existing consecutive glare rows appear as a combined row; keep their combined field count unchanged when editing. Moving or using `Delete Glare` affects the entire glare group. After cell or column operations, click `Apply`, then `Save`.

A map can load one resource list of each type. To add a missing list, click `New or Import File` in the table. To replace it, right-click `Source path` at the top and choose `Change File...`. If that list has unapplied or unsaved changes, the program asks for confirmation before discarding that list's changes.

komapedit supports Station List 0.04+, Structure List 1.00+, and Signal Aspects List and Sound List 2.00+. Ordinary and 3D sounds use the same Sound List format. The standard resource-list comment marker is `#`; komapedit also accepts `//` comments outside CSV double quotes.

### 7. File Structure Diagram and Text Preview

Open this window from `Auxiliary Info -> Other -> File Structure Diagram`. The entry map is on the left, and its included submaps are shown by level to the right. Hover over a node to see the Include argument and absolute path.

#### Viewing Files

- Right-click a valid node and choose `Preview Text` to view its source and line numbers in the read-only Text Preview.
- The source filename in `Properties/Edit` also offers `Preview Text`. Text previews of maps and resource lists refresh on `Apply`, so you can inspect the resulting source before saving.
- `Open in File Explorer` opens the target directory; a missing directory is reported in the Console.
- Missing or invalid Include targets are red. Text preview, submap import, and new-submap actions are disabled for them.

#### Editing Includes

The following actions require editing to be enabled:

- **Change Included File...**: Selects a new `.txt` or `.csv` submap and rewrites the Include path in its parent file. A relative path is preferred; an absolute path is used if needed.
- **Unlink Include**: Deletes the Include statement from its parent. The action is blocked if later statements still depend on data from that submap.
- **Import Submap...**: Selects an existing BVE map and stages a new Include in the source file represented by the current node.
- **New Submap...**: Creates a blank submap at a new file path with the `BveTs Map 2.02:utf-8` header, UTF-8 without BOM, and CRLF line endings, then stages its Include.

A new Include goes after the last Include preceding the first local distance statement. With no preceding Include, it goes before the first distance statement; with neither, it is appended to the file. Changes update the preview immediately and are written to the parent map on `Save`.

In komapedit, each Map file's `distance` starts at `0`; returning from an Include restores the parent's mileage, while ordinary `$variables` are shared. To position a submap relative to its parent, assign `$dis=distance;` before the Include, then use `$dis+offset;` to set mileage in the submap.

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
- Right-click a signal and choose `Switch Signal Aspect` to switch its preview model by aspect index and structure key.
- With editing enabled, the same menu also provides `Properties/Edit` and delete actions. See section 10 for edit gizmos.

#### Mileage Select Mode

- The nearest whole-meter cross-section on the own-track plane and its mileage label appear near the pointer.
- With editing enabled, right-click this position and choose `Add Map Element at Current Mileage`. The wizard fills in `distance` automatically.

#### Scene Overlay and Settings

The canvas shows the camera position, current curve radius and cant, gradient, speed limit, section signal speed, and next-station information. A `Curve.Interpolate` interval shows both endpoints' radii, cants, and curve directions; when both radii are zero, it shows `Straight`. The bottom shows scene chunks, instances, loaded models, and frame rate.

FPS shows the scene canvas's refresh rate; the last value is kept while idle.

Use `Options -> 3D Canvas Settings` to adjust:

- **Draw distance**: Controls the scene range ahead of the camera. Enable `Adjust draw distance from map statements` to also limit the range using `DrawDistance.Change`.
- **Fog effect**: Toggles the map's fog preview.
- **Edit component size and Camera movement speed**: Adjust the gizmo size and keyboard movement speed.
- **Performance warning**: Colors the currently drawn instance count yellow above the `Warning threshold` and red above the `Critical warning threshold`.

Related marker visibility stays synchronized with `Auxiliary Info`.

Fog preview supports exponential `Fog.Interpolate`/`Fog.Set` and the compatibility statement `Legacy.Fog` for linear fog. Fog interpolates between nodes of the same type; different types switch at the later node. `Legacy.Fog` start/end values are camera-depth distances in meters, and RGB uses the 0–255 scale. Settings at mileage zero take effect immediately; later settings transition over 25 meters when both states are linear. Edit either fog type through its table or markers, or create it in the wizard's Effects category.

#### Scene Boards

Use `Auxiliary Info` to control board visibility. With `Curve Radius` enabled, `Curve.Interpolate` points show radius, cant, and curve direction; a zero radius shows `Intpl. 0`. Boards support picking and highlighting, plus right-click editing and deletion when editing is enabled.

### 10. Editing, Creating, and Saving

#### Before Editing

The first time you turn on `Enable Edit` on the toolbar, a prompt appears. You can select `Don't show again` and confirm.

After enabling editing, wait for edit data to finish loading before making changes. After entering values in `Properties/Edit`, Light Sources, or a wizard, click `Apply` before leaving the window. When you disable editing, open another document, reload, or exit, the program prompts you to handle pending changes.

#### Drafts, Apply, Save, and Revert

Map and resource-list editing follows these steps:

1. **Edit the draft**: Enter values in `Properties/Edit`, a resource-list table, or a wizard. Some objects provide a live preview.
2. **Apply to preview**: Click `Apply` in the window to validate and reparse the in-memory working copy, then refresh the 2D View, tables, and 3D preview.
3. **Save to disk**: Click toolbar `Save` or press `Ctrl+Shift+S` to write all applied changes to the corresponding map, Include, or resource-list files.

Apply all resource-list and Custom Messages drafts before saving. Changes in Scenario input boxes are saved directly by toolbar `Save`. Toolbar `Revert` restores the in-memory state from the last save; use `Reload` to read changes written by an external editor.

Before saving, the program reparses and verifies the edited result, preserving the original encoding, BOM, and line endings. If the new text cannot be represented in the original encoding, or another program has changed a file, saving is blocked with an explanation.

#### Editing or Deleting Existing Map Elements

1. Right-click an item in a table or a 2D/3D marker and choose `Properties/Edit`.
2. Check the source file, source position, and raw statement in the window, then change the fields.
3. Click `Apply` to refresh the in-memory preview.
4. Check the 2D View, tables, and 3D result. If they are correct, use toolbar `Save`.

Editable items include stations, structures, signals, beacons, Repeaters, Sections, speed limits, curves and gradients, other tracks, track irregularity, sounds, backgrounds, adhesion, cab illuminance, fog, lighting parameters, draw distance, and PreTrain pass points. Choose `Delete` from the context menu to apply a deletion to the preview, then save it to the source file.

Use `Add Coordinate Offsets` or `Remove Coordinate Offsets` to switch between `Structure.Put`/`Put0` or `Repeater.Begin`/`Begin0`. Removing nonzero offsets requires confirmation. Editing Z, rotation, tilt, or span in a short-form `Signal.Put` requires confirmation to convert it to the full form.

#### Editing Repeater Segments

- Open a Repeater's `Properties/Edit` and use `Previous` and `Next` to move between segments with the same key.
- Use `+`, `-`, and the up/down arrows beside the structure-key list to add, remove, and reorder the models placed in a cycle.
- `Insert Change Point` opens a wizard prefilled with the current parameters. Enter the new mileage and parameters, then apply. If the last segment has no End, use `Add End Position` to add one.
- For multi-segment intervals, the delete menu offers `Delete All`, `Delete Change Point`, `Trim to Change Point`, and `Start from Change Point` to remove the whole chain, a single change point, the following segments, or the preceding segments respectively. These actions update the preview; save afterward.

#### Live Adjustment in the 3D Scene

Open `Properties/Edit` and drag a supported object's gizmo to adjust its draft, then apply and save as described above.

- `Structure.Put`, full-form `Signal.Put`, and `Repeater.Begin` use X/Y/Z axes to change coordinates.
- For `Sound3D[soundKey].Put(x, y)`, X/Y adjust the relative position in 0.001 m steps, and Z adjusts mileage in whole meters.
- `Structure.Put0` and `Repeater.Begin0` use Z to adjust mileage in whole meters. Adding coordinate offsets enables the full coordinate gizmo.
- A Repeater with an explicit End provides a Z axis at its end position to adjust the end mileage in whole meters.
- `Structure.PutBetween` uses a Z axis to change whole-meter mileage and recalculates the deformed model live.

#### New Map Element Wizard

Open the wizard from toolbar `Add Map Element`, or right-click the current mileage in 3D `Mileage Select` mode. Choose a target source file and template, then enter the parameters. Any loaded map source file, including a blank or distance-free file, can be a target.

When creating or moving an element, the program arranges distance statements automatically while preserving other statements and comments. If manual placement is needed, select a highlighted statement boundary in Text Preview and click `OK` to continue. If a distance expression is required, enter its source text in the prompt and click `Apply`.

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

- **Repeater**: Add Begin, End, or both. When added as a pair, the end mileage must be at least the start mileage, and paired intervals with the same key must avoid overlap. Use `Insert Change Point` for parameter changes within an interval.
- **Curves and gradients**: Add a start, an end, or both. Curves start with `Curve.Begin` or `Curve.Change` and end with `Curve.End`; gradients use `Gradient.Begin`/`End`. Begin/End can include the corresponding transition starts. `Curve.Change` changes the radius directly.
- **Curve interpolation points**: `Curve.Interpolate` requires distance; radius and cant both default to `0`. Uncheck cant for the one-argument form, or radius for the zero-argument form.
- **Other tracks**: Enable optional trailing arguments in order; for example, include `radiusH` before `radiusV`. A new trackKey creates another track. Numeric keys and quoted string keys are treated separately.
- **PreTrain pass points**: Enter `passTime` as `hh:mm:ss` or seconds since midnight. Hover over the input box for format hints.
- **Lighting**: The wizard creates lighting statements at mileage `0`; enter RGB values from `0` to `1`. Each lighting statement type has one definition across the entire map, including Includes.

#### Custom Messages (Message from Creator)

Choose `Other -> Custom Messages` in the New Map Element Wizard, select a map source file, and enter a single-line message. After applying and saving, the message is written after the blank line below the file header in this comment format, or appended to an existing message block:

```plaintext
BveTs Map 2.02

//--kme--message-from-creator:"content"
```

Messages are stored as ordinary BVE `//` comments. Contents are literal, preserving spaces, quotes, and backslashes. Empty messages are allowed; content must be a single line without NUL characters.

Opening a map or Scenario displays messages from the root map and its loaded Includes, with paging for multiple messages. Repeated references to the same file show each message once. Select `Do not show again` and confirm to remember that choice for the root map.

Open `Auxiliary Info -> Other -> Custom Messages` at any time to view the source file, line number, and content. Enter a keyword and press `Enter` or click `Find` to search source filenames or message contents. Use the up and down arrows to move between results.

With editing enabled, double-click a content cell to edit it; `Enter` or leaving the cell stages the draft, while `Esc` cancels the current input. Choose `Delete Entire Row` from the cell's context menu to stage deletion. Click the tab's `Apply`, then toolbar `Save`.

#### New File Wizard

Open `File -> New...` to create a Map, Scenario, Structure List, Signal Aspects List, Sound List, Sound List for Sound3D, or Station List.

1. Choose the file type, enter a file name without an extension, choose `.txt` or `.csv`, and select a directory.
2. For a resource list, use `Import File` to prefill the name, directory, and suffix from an existing `.txt` or `.csv`, then adjust them as needed.
3. For a Scenario, fill in the desired fields; empty fields are omitted. Use `Select File` to fill `Route`, `Vehicle`, or `Image` with a relative path.
4. If a map is loaded, creating a Map or resource list requires editing to be enabled; confirm the target source file under `Reference in`. With no map loaded, you can create a standalone file directly.
5. Click `Confirm` to create or reuse the file. After creating a submap, save its reference in the parent map before opening it. Standalone maps and Scenarios also offer `Confirm and Load`.

Existing regular files are reused as they are. New files use UTF-8 and CRLF: maps and resource lists contain the standard header, while Scenarios write their contents in standard field order.

The Scenario template provides eight fields: `Title`, `Route`, `RouteTitle`, `Vehicle`, `VehicleTitle`, `Author`, `Image`, and `Comment`. Each path field accepts one path, and field values must avoid comment characters such as `#` and `;`. Edit multiple path candidates and weights in the Scenario File tab after loading.

The wizard creates the file immediately. Its `include` or corresponding `*.Load` reference is applied to the map preview and written by toolbar `Save`. A map can reference one resource list of each type. To replace a referenced file, use the `Source path` context menu at the top of its table.

## Appendix: CSV Data Formats

### Own-Track Geometry CSV (Export)

Export the current geometry samples with `File -> Export CSV...`. File name format:

```text
<output-folder-name>_owntrack.csv
```

Header:

```csv
#distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge
```

Field reference:

| Field            | Description                                                                 |
| ---------------- | --------------------------------------------------------------------------- |
| distance         | Absolute map distance, in meters                                            |
| x                | Own-track plan X coordinate after gradient projection, in meters            |
| y                | Own-track plan Y coordinate after gradient projection, in meters            |
| z                | Elevation, in meters                                                        |
| direction        | Track direction angle, in radians                                           |
| radius           | Signed curve radius, in meters: positive for right curves, negative for left curves, and 0 for straight track |
| gradient         | Gradient, in per mille (‰)                                                  |
| interpolate_func | Interpolation type: `0` means sinusoidal half-wave easing, `1` means linear easing |
| cant             | Cant, in meters                                                             |
| center           | Lateral coordinate of the cant rotation center, in meters                   |
| gauge            | Track gauge, in meters                                                      |

### Other-Track Geometry CSV (Export)

Each other track is exported as a separate CSV file. File name format:

```text
<output-folder-name>_<trackKey>.csv
```

The characters `\ / : * ? " < > |` in trackKey are replaced with `_`; an empty key uses `root`. Output filenames must be unique across tracks.

Header:

```csv
#distance,x,y,z,interpolate_func,cant,center,gauge
```

Field reference:

| Field            | Description                                                                 |
| ---------------- | --------------------------------------------------------------------------- |
| distance         | Absolute map distance, in meters                                            |
| x                | Other-track plan X coordinate derived from the projected own track, in meters |
| y                | Other-track plan Y coordinate derived from the projected own track, in meters |
| z                | Other-track elevation, in meters                                            |
| interpolate_func | Cant interpolation type: `0` means sinusoidal half-wave easing, `1` means linear easing |
| cant             | Cant, in meters                                                             |
| center           | Lateral coordinate of the cant rotation center, in meters                   |
| gauge            | Track gauge, in meters                                                      |

CSV export contains track geometry, with numeric values formatted to six decimal places.
