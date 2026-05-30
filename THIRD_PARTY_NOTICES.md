# Third-Party Notices

This file lists third-party source code and reference projects used by komapedit.
Keep this file together with source archives and binary distributions of
komapedit.

## Summary

| Component | Use in komapedit | License | Notice |
| --- | --- | --- | --- |
| [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) | Reference/derived track-geometry and BVE map parsing behavior | Apache License 2.0 | Copyright (c) 2021-2024 konawasabi |
| [Dear ImGui](https://github.com/ocornut/imgui) | Docking GUI, Win32 backend, DirectX 11 backend, C++ std::string helper | MIT License | Copyright (c) 2014-2026 Omar Cornut |
| [ImPlot](https://github.com/epezent/implot) | 2D plotting widgets | MIT License | Copyright (c) 2020 Evan Pezent |
| stb single-file libraries bundled with Dear ImGui | Font/text/rectangle-packing code used by Dear ImGui | MIT License or Public Domain | Copyright (c) 2017 Sean Barrett |

The full Apache License 2.0 text is in LICENSE. The local third-party source
trees also contain their upstream license files:

- `third_party/imgui/LICENSE.txt`
- `third_party/implot/LICENSE`
- `third_party/imgui/imstb_rectpack.h`
- `third_party/imgui/imstb_textedit.h`
- `third_party/imgui/imstb_truetype.h`

Win32, WIC, DirectX 11, CMake, Ninja, Git, MSVC, and MinGW are platform SDKs,
build tools, or toolchains referenced by the build instructions. This repository
does not vendor their source code.

## MIT License Notice

The following MIT License text applies to Dear ImGui, ImPlot, and the stb
components listed above when the MIT option is used.

Copyright notices:

- Copyright (c) 2014-2026 Omar Cornut
- Copyright (c) 2020 Evan Pezent
- Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
