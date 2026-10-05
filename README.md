<p align="center">
    <img src="icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit is a lightweight Windows viewer and editor for BVE Trainsim map files, with 2D route views, 3D scene previews, and source-backed map editing.

Editing is experimental. Back up route files or keep them under version control before use. See [Current BVE Map Syntax Support](#current-bve-map-syntax-support) for supported statements and [TODO.md](TODO.md) for development progress.

## Documentation

- [简体中文README](docs/README_zhcn.md)
- [User manual](docs/user-manual.md)
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

- Read/preview: parse data and display it through track geometry, tables, markers, or the 3D scene.
- Basic editing: modify existing content through property windows, tables, or file-action menus and save it.
- Create: use the New Map Element wizard, New File wizard, File Structure Diagram, or resource tables to create statements, files, and list rows.
- Graphical editing: drag elements or use gizmos directly on the 2D/3D canvas.
- √ = supports the features and forms listed in the row; △ = limited forms, fields, or linked operations are available; ✕ = currently unsupported; - = not applicable.

Ratings cover all current UI entry points. When a row lists multiple overloads or aliases, creation is marked △ if only some forms can be created. `*.Load` rows cover the reference path, list file, and list contents; see the notes for the scope. The table includes [official BVE map syntax](https://bvets.net/jp/edit/formats/route/map.html), legacy aliases, and explicitly marked project compatibility statements or comments.

| Map syntax                                                                                                                                                                                  | Read/preview | Basic editing | Create | Graphical editing | Notes |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :-----: | :-----------: | :---------: | :---------------: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| File header, version, and encoding                                                                                                                                                          | △ | ✕ | △ | - | Reads BVE Map 2.0+ in UTF-8, UTF-16LE/BE, and CP932/Shift_JIS; new maps use the `BveTs Map 2.02:utf-8` header |
| Comments and basic statement syntax                                                                                                                                                         |    √    |       ✕       |      -      |         -         | Supports `#`/`//` comments, semicolon separation, keyed and nested elements, and multiline statements; names are case-insensitive |
| [project comment] `//--kme--message-from-creator:"content"` | √ | √ | √ | - | Stores messages in ordinary BVE comments; displays them by default when opening a map, supports editing/deletion in Custom Messages, and creation in the Other wizard category |
| Variables in assignments, arguments, and keys                                                                                                                                               | √ | ✕ | ✕ | - | Evaluated during parsing; the read-only variable table groups assignments and source locations by name |
| Arithmetic operators (`+`, `-`, `*`, `/`, `%`)                                                                                                                                              | √ | △ | △ | - | Supports arithmetic, unary signs, parentheses, and `+` string concatenation; numeric expressions can be entered during distance-expression confirmation when editing or creating elements |
| Distance declarations and `distance` expressions                                                                                                                                            | √ | △ | △ | △ | Edit or create distance blocks through supported elements; when prompted, enter a distance expression that evaluates to the target mileage. Some 3D objects use a Z-axis gizmo to change mileage in whole meters |
| Mathematical functions                                                                                                                                                                      | √ | △ | △ | - | Supports `rand`, `abs`, `sin`, `cos`, `atan2`, `sqrt`, `exp`, `log`, `floor`, `ceil`, and `pow`; functions can be used during distance-expression confirmation |
| `include 'file';`                                                                                                                                                                           | √ | √ | √ | - | Replace, unlink, import, or create submap references in the File Structure Diagram; the New File wizard also adds Includes. Missing or invalid submaps are skipped with a warning |
| `Curve.SetGauge(value)` / `[legacy] Curve.Gauge(value)`                                                                                                                                     | √ | √ | △ | ✕ | Edit distance and gauge; the wizard creates `SetGauge` with default `1.067`; `CG` boards can be shown in 2D/3D |
| `Curve.SetCenter(x)`                                                                                                                                                                        |    √    |       √       |      √      |         ✕         | Edit distance and cant-center offset; the creation default is `0`; `CC` boards can be shown in 2D/3D |
| `Curve.SetFunction(id)`                                                                                                                                                                     |    √    |       √       |      √      |         ✕         | Edit distance and interpolation function with `id` set to `0` or `1`; the creation default is `0`; `CF` boards can be shown in 2D/3D |
| `Curve.BeginTransition()`                                                                                                                                                                   |    √    |       △       |      △      |         ✕         | Edit an existing transition's distance through its paired Begin/End; create it with a canted Begin or an End. Start/end transition options are linked, with separate distances |
| `Curve.Begin(radius, cant)` / `[legacy] Curve.BeginCircular(radius, cant)`                                                                                                                  |    √    |       √       |      △      |         ✕         | The wizard creates a canted Begin together with a preceding transition; existing legacy BeginCircular statements can be edited |
| `Curve.Begin(radius)` / `Curve.Change(radius)`                                                                                                                                              |    √    |       √       |      √      |         ✕         | The wizard offers Begin or Change and can add an end position at the same time |
| `Curve.End()`                                                                                                                                                                               |    √    |       √       |      √      |         ✕         | Create alone or with a start position, optionally with a preceding transition |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()`                                                                                                     |    √    |       √       |      √      |         ✕         | Supports 0/1/2 arguments, preserving the argument count during editing; enable Curve Radius to edit or delete through 2D/3D markers |
| `Gradient.BeginTransition()`                                                                                                                                                                |    √    |       △       |      △      |         ✕         | Edit an existing transition's distance through its paired Begin/End; create it with Begin/End. Start/end transition options are linked, with separate distances |
| `Gradient.Begin(gradient)` / `[legacy] Gradient.BeginConst(gradient)`                                                                                                                       | √ | √ | △ | ✕ | The wizard can add Begin alone or together with End and preceding transitions; existing legacy BeginConst statements can be edited |
| `Gradient.End()`                                                                                                                                                                            |    √    |       √       |      √      |         ✕         | Create alone or with Begin, optionally with a preceding transition |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()`                                                                                                                                 |    √    |       ✕       |      ✕      |         ✕         | Both 0/1-argument forms feed track geometry |
| `Legacy.Turn`, `Legacy.Curve`, `Legacy.Pitch`                                                                                                                                               |    √    |       △       |      ✕      |         ✕         | Project compatibility statements, all used for own-track geometry; existing Legacy.Curve values and distances can be edited |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()`                                                                         |    √    |       △       |      √      |         ✕         | Supports all argument forms; edit distance and values or delete statements. An individual statement's `trackKey` is read-only; rename the whole track from Other Tracks |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()`                                                                         |    √    |       △       |      √      |         ✕         | Supports all listed argument forms, with the same editing scope as X.Interpolate |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)`                                                           |    √    |       △       |      √      |         ✕         | Supports all argument forms; edit distance and values or delete statements, preserving the argument count. Key renaming follows X.Interpolate |
| `Track[trackKey].Cant.SetGauge(gauge)` / `[legacy] Track[trackKey].Gauge(gauge)`                                                                                                            | √ | △ | △ | ✕ | New statements use Cant.SetGauge; edit values and distance or delete either form. Key renaming follows X.Interpolate |
| `Track[trackKey].Cant.SetCenter(x)`                                                                                                                                                         |    √    |       △       |      √      |         ✕         | Edit values and distance or delete statements; key renaming follows X.Interpolate |
| `Track[trackKey].Cant.SetFunction(id)`                                                                                                                                                      |    √    |       △       |      √      |         ✕         | New statements require `id` to be `0` or `1`; edit values and distance or delete statements. Key renaming follows X.Interpolate |
| `Track[trackKey].Cant.BeginTransition()`                                                                                                                                                    |    √    |       △       |      √      |         ✕         | Edit distance or delete statements; key renaming follows X.Interpolate |
| `Track[trackKey].Cant.Begin(cant)`                                                                                                                                                          |    √    |       △       |      √      |         ✕         | Edit values and distance or delete statements; key renaming follows X.Interpolate |
| `Track[trackKey].Cant.End()`                                                                                                                                                                |    √    |       △       |      √      |         ✕         | Edit distance or delete statements; key renaming follows X.Interpolate |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / `[legacy] Track[trackKey].Cant(cant)`                                                                     | √ | △ | △ | ✕ | New statements use 0/1-argument Interpolate; edit existing values and distance or delete statements, preserving argument count. Key renaming follows X.Interpolate |
| `Structure.Load(filePath)`                                                                                                                                                                  | √ | √ | √ | - | Replace the list path, create or import a list, and add its Load reference. Edit, add, delete, or reorder key/path rows, including the first row of an empty list. List-version compatibility: 1.00+ |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                                                                                    |    √    |       √       |      √      |         △         | All fields are editable; 3D gizmos adjust X/Y/Z |
| `Structure[structureKey].Put0(trackKey, tilt, span)`                                                                                                                                        |    √    |       √       |      √      |         △         | Properties/Edit converts between Put0 and Put; the Put0 Z-axis gizmo adjusts distance in whole meters |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)`                                                               | √ | √ | △ | △ | Both forms are editable; new statements use three arguments. Property drafts update the 3D shape live, and the Z-axis gizmo changes mileage in whole meters |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` | √ | √ | √ | △ | Edit all parameters, the structure-key list, and the whole chain's key; supports Begin/Begin0 conversion, change points, linked deletion, and position gizmos. Create alone or paired with End; same-key paired intervals must not overlap |
| `Repeater[repeaterKey].End()`                                                                                                                                                               |    √    |       △       |      √      |         △         | Edit end mileage through the owning Begin or its End gizmo; create alone or as a pair. An isolated End has no separate property window; an additional End inside a closed interval with the same key is rejected |
| `Background.Change(structureKey)`                                                                                                                                                           |    √    |       √       |      √      |         ✕         | Edit distance and key; preview the background in the scene |
| `Station.Load(filePath)`                                                                                                                                                                    | √ | √ | √ | - | Replace the list path, create or import a list, and add its Load reference. Edit all 13 fields, add, delete, or reorder station rows, including the first row of an empty list. List-version compatibility: 0.04+ |
| `Station[stationKey].Put(door, margin1, margin2)`                                                                                                                                           |    √    |       √       |      √      |         ✕         | Edit distance, key, door side, and stop tolerances; requires `margin1 < 0` and `margin2 > 0` |
| `Section.Begin(...)` / `[legacy] Section.BeginNew(...)`                                                                                                                                     |    √    |       √       |      √      |         ✕         | Edit distance and the signal-index list; markers appear in 2D/3D |
| `Section.SetSpeedLimit(...)` / `[legacy] Signal.SpeedLimit(...)`                                                                                                                            |    √    |       √       |      √      |         ✕         | Edit distance and the speed-limit list; effective values appear in the 3D signal summary |
| `Signal.Load(filePath)`                                                                                                                                                                     | √ | △ | √ | - | Replace, create, or import an aspect list and add its Load reference; add main rows, glare rows, and columns. Up to 509 structure-key columns are shown; when multiple glare rows are retained, their combined field count must stay unchanged |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                  | √ | √ | △ | △ | Both forms are editable; new statements use the full form. Extending a short form requires conversion confirmation; 3D gizmos adjust X/Y/Z |
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
| `Sound.Load(filePath)`                                                                                                                                                                      | √ | √ | √ | - | Replace the list path, create or import a list, and add its Load reference. Edit keys, paths, and buffer counts; add, delete, or reorder rows, including the first row of an empty list. List-version compatibility: 2.00+ |
| `Sound[soundKey].Play()`                                                                                                                                                                    |    √    |       √       |      √      |         ✕         | View sound events in tables and markers; edit distance and key |
| `Sound3D.Load(filePath)`                                                                                                                                                                    | √ | √ | √ | - | Replace, create, or import a 3D sound list and add its Load reference. Edit keys, paths, and buffer counts; add, delete, or reorder rows, including the first row of an empty list. List-version compatibility: 2.00+ |
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
