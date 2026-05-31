# Third-Party Notices

This file lists third-party source code, linked libraries, and reference
projects used by komapedit. Keep this file together with source archives and
binary distributions of komapedit.

## Summary

| Component | Use in komapedit | License | Notice |
| --- | --- | --- | --- |
| [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) | Reference/derived track-geometry and BVE map parsing behavior | Apache License 2.0 | Copyright (c) 2021-2024 konawasabi |
| [Dear ImGui](https://github.com/ocornut/imgui) | Docking GUI, Win32 backend, DirectX 11 backend, C++ std::string helper | MIT License | Copyright (c) 2014-2026 Omar Cornut |
| [ImPlot](https://github.com/epezent/implot) | 2D plotting widgets | MIT License | Copyright (c) 2020 Evan Pezent |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | Structure model import for the 3D preview | Modified BSD 3-Clause License | Copyright (c) 2006-2026, assimp team |
| stb single-file libraries bundled with Dear ImGui | Font/text/rectangle-packing code used by Dear ImGui | MIT License or Public Domain | Copyright (c) 2017 Sean Barrett |

The full Apache License 2.0 text is in LICENSE. The local third-party source
trees also contain their upstream license files:

- `third_party/imgui/LICENSE.txt`
- `third_party/implot/LICENSE`
- `third_party/imgui/imstb_rectpack.h`
- `third_party/imgui/imstb_textedit.h`
- `third_party/imgui/imstb_truetype.h`

Assimp is not vendored in this repository. When distributing binaries that
include or require Assimp runtime DLLs, include the Assimp license/copyright
file supplied by Assimp or by the package manager used for the build.

Win32, WIC, DirectX 11, CMake, Ninja, Git, MSVC, and MinGW are platform SDKs,
build tools, or toolchains referenced by the build instructions. This repository
does not vendor their source code.

## Assimp BSD 3-Clause Notice

The following notice applies when Assimp runtime libraries are distributed with
komapedit binaries.

Open Asset Import Library (assimp)
Copyright (c) 2006-2026, assimp team
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the assimp team, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
INCLUDING NEGLIGENCE OR OTHERWISE ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
