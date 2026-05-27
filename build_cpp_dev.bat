@echo off
setlocal
cd /d "%~dp0"

set CMAKE_ARGS=-S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
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

cmake --build build --config Debug
if errorlevel 1 (
    echo cmake build failed.
    exit /b 1
)

echo kobushiCPP dev built: %cd%\build

pause
