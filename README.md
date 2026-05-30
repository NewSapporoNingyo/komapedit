# komapedit
[简体中文Readme](README_zhcn.md)
## Project Overview

komapedit is a lightweight viewer and editor for BVE Trainsim map files. It reworks the track-geometry approach from `kobushi-trackviewer` as a C++/Win32 desktop application. The current version focuses on loading maps, generating track geometry, showing 2D plan/profile/radius views, displaying map data tables, and exporting track geometry to CSV.

The application has two main components:

- `maploader.dll`: loads `BveTs Map` files, parses part of the BVE Map syntax, generates own-track and other-track geometry, and exposes an intermediate JSON representation.
- `komapedit.exe`: the desktop GUI, built with Dear ImGui, ImPlot, Win32, and DirectX 11.

At this stage, komapedit is closer to a map inspector and track-geometry visualizer than a full map editor. Full object editing, 3D previews, sound editing, and environmental-effect editing are still planned.

## Development Status (TODO List)

### Map Loading and Parsing

- [x] Load `BveTs Map 2.0+` map files.
- [x] Handle UTF-8, UTF-8 with BOM, UTF-16LE, UTF-16BE, CP932/Shift_JIS, and related text encodings.
- [x] Support `Include` references to other map files.
- [x] Support `$variable = expression;`, the predefined `distance` variable, and basic math functions.
- [x] Support `#`, `//`, and `/* ... */` comments.
- [x] Load maps asynchronously and show logs, warnings, and errors in the console window.
- [ ] Save changes back to map files.

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
- [x] Support jumping to stations.
- [x] Import a background image and adjust its position, size, rotation, and brightness.
- [x] Align a background image using two station positions.
- [ ] Structure and repeater placement markers on the plan view.
- [ ] Cab-brightness setting position markers.
- [ ] Fog-effect change position markers.
- [ ] Signal position markers.
- [ ] Beacon position markers.
### Map Data Tables

- [x] Load the station list specified by `Station.Load`.
- [x] Display the station list alongside `Station.Put` placement data from the map.
- [x] Display the other-track list, with controls for visibility, range, and color.
- [x] Display map Structure placement tables for `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween`.
- [x] Load and display Structure model lists referenced by `Structure.Load` (`.txt` or `.csv`).
- [x] Display `Repeater.Begin`, `Repeater.Begin0`, and `Repeater.End` data, with Begin/End distances merged for readability.
- [x] Right-click source file paths in the Structure and repeater tables to open their folders in File Explorer.
- [ ] Edit Structure model lists.
- [ ] Edit station lists and station positions.
- [ ] Display/Edit signal lists.
- [ ] Display/Edit Beacon lists.
### 3D Canvas

- [ ] 3D preview for Structure models.
- [ ] 3D scene preview canvas.
- [ ] Structure position editing tools in the 3D canvas.

### Environmental Effects

- [ ] Load and edit running sounds, switch sounds, station announcement sounds, and 3D sounds.
- [ ] Edit cab-brightness setting positions.
- [ ] Edit fog effects.

### User Interface and Utilities

- [x] Dear ImGui docking-based multi-window layout.
- [x] UI language switching between Simplified Chinese, English, and Japanese.
- [x] Settings for font size, UI component size, station marker size, and theme color.
- [x] Recent-map history.
- [x] Save background-image parameters with recent-map entries in `history.ini`.
- [x] Save settings to `settings.ini` next to the executable.
- [x] Export own-track and other-track geometry to CSV.

## Installation and Startup

This repository does not currently provide a standalone installer. The recommended workflow is to build from source and run the generated executable.

After building, make sure `komapedit.exe` and `maploader.dll` are in the same directory, then run `build_release\komapedit.exe`.

On startup, the application creates or reads the following files next to the executable:

- `imgui.ini`: stores UI window positions and related ImGui layout data.
- `settings.ini`: stores settings such as UI language, font size, component size, station marker size, and theme color.
- `history.ini`: stores recent maps and background-image alignment parameters.

## Usage

1. Use `File -> Open...` or the `Open` button on the toolbar to select a `.txt` map file.
2. After the map loads, the main window shows the plan view, profile chart, and curve-radius chart.
3. In the plan view:
   - Drag with the left mouse button to pan.
   - Use the mouse wheel to zoom.
   - Hold `Shift` while using the mouse wheel to rotate, or drag with the right mouse button / `Ctrl + left mouse button`.
   - Double-click the plan view to fit the map to the current viewport.
4. Use `Station Jump` on the toolbar to jump to a station by mileage.
5. Use the `2D View` menu to toggle stations, station names, mileage labels, curve display, speed limits, the profile chart, the curve-radius chart, and other-track profile display.
6. Switch `Mode` to `Measure`, then move near the track or double-click to view mileage, elevation, gradient, curve radius, and speed limit.
7. Use the `Map Info` menu to open:
   - `Other Tracks`: toggle other-track display and adjust visible range and color.
   - `Station List`: view the station list and `Station.Put` placement data.
   - `Map Structure List`: view `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween` entries from the map.
   - `Structure Model List`: view the structure keys and model files from the `Structure.Load` structure list.
   - `Repeater List`: view merged `Repeater.Begin/End` data.
8. Use `2D View -> Background Image` to import a background image. You can adjust its position, size, rotation, and brightness manually, or align it using two stations.
9. Use `File -> Export CSV...` to choose an output folder and export own-track and other-track geometry CSV files.
10. Press `F5` or use `File -> Reload` to reload the current map.

## Project Layout

```text
komapedit/
├─ CMakeLists.txt                  # CMake build configuration
├─ README.md                       # Project documentation
├─ LICENSE                         # Apache License 2.0
├─ NOTICE                          # Project copyright and Apache attribution notices
├─ THIRD_PARTY_NOTICES.md          # Third-party library and reference-project notices
├─ build_dev.bat                   # Debug build script
├─ build_release.bat               # Release build script
├─ clear_build_release_dist.bat    # Cleans Release output, keeping distributable binaries and notices
├─ get_3rd_party_packages.bat      # Fetches ImGui and ImPlot
├─ include/
│  ├─ maploader.h                  # maploader C ABI
│  └─ multilanguage.h              # UI localization strings
├─ src/
│  ├─ maploader.cpp                # BVE Map parsing and geometry generation
│  └─ gui_app.cpp                  # Win32/DirectX 11/ImGui GUI
├─ third_party/
│  ├─ imgui/                       # Dear ImGui, docking branch
│  └─ implot/                      # ImPlot
├─ build/                          # Debug build output, generated locally
└─ build_release/                  # Release build output, generated locally
```

## Building From Source

### Requirements

- Windows
- [CMake](https://cmake.org/) 3.20 or newer
- [Ninja](https://github.com/ninja-build/ninja)
- A C++17-capable compiler, such as MSVC or [MinGW](https://www.mingw-w64.org/)
- Windows SDK / DirectX 11 development libraries
- Git, used to fetch third-party dependencies

### Fetch Third-Party Dependencies: `get_3rd_party_packages.bat`

This script clones the following two projects. Make sure Git is installed first.

- `third_party/imgui` -> the `docking` branch of `ocornut/imgui`
- `third_party/implot` -> `epezent/implot`

The cloned third-party source trees are ignored by Git. Their upstream license
files remain under `third_party/`; distribution notices are summarized in
`THIRD_PARTY_NOTICES.md`.

### Debug Build: `build_dev.bat`

The output directory is `build`.

### Release Build: `build_release.bat`

The output directory is `build_release`. The main build products are:

- `komapedit.exe`
- `maploader.dll`
- `LICENSE`
- `NOTICE`
- `THIRD_PARTY_NOTICES.md`

To clean the Release output for distribution, run `clear_build_release_dist.bat`.

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

Exported numeric values use fixed six-decimal formatting. CSV export currently includes track geometry only; it does not export stations, Structures, repeaters, sounds, fog, or 3D scene data.

## License and Third-Party Notices

komapedit is distributed under the Apache License, Version 2.0. See `LICENSE`
for the license text and `NOTICE` for project attribution notices.

This project is based on `kobushi-trackviewer` and is intended to help inspect
and edit BVE Trainsim map files. The C++/Win32 implementation and modifications
are by Sapporo_ningyo.

Reference project:

| Project | Copyright | License |
| --- | --- | --- |
| [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) by konawasabi | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

Third-party libraries used by the GUI:

| Library | Use | Copyright | License |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | Docking GUI, Win32 backend, DirectX 11 backend, C++ std::string helper | Copyright (c) 2014-2026 Omar Cornut | MIT License |
| [ImPlot](https://github.com/epezent/implot) | 2D plotting widgets | Copyright (c) 2020 Evan Pezent | MIT License |
| stb single-file libraries bundled with Dear ImGui | Font/text/rectangle-packing support used by Dear ImGui | Copyright (c) 2017 Sean Barrett | MIT License or Public Domain |

When distributing source or binaries built from this repository, include
`LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`. If the `third_party/`
source trees are distributed, keep their original license files and copyright
notices intact.

Project page: <https://github.com/NewSapporoNingyo/komapedit>
