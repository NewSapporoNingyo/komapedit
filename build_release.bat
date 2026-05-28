@echo off
setlocal
cd /d "%~dp0"

set CMAKE_ARGS=-S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
if defined NINJA_EXE (
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_MAKE_PROGRAM=%NINJA_EXE%
)
if defined VCPKG_ROOT (
    set CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
)

cmake %CMAKE_ARGS%
if errorlevel 1 (
    echo cmake configure failed.
    exit /b 1
)

cmake --build build_release --config Release
if errorlevel 1 (
    echo cmake build failed.
    exit /b 1
)

echo kobushiCPP release built: %cd%\build_release

pause
