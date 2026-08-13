@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

for %%F in (settings.ini history.ini imgui.ini) do (
    if exist "build\%%F" (
        echo Unsupported obsolete settings file: build\%%F
        echo Remove it manually and use the canonical build\settings layout.
        exit /b 1
    )
)
for %%F in ("build\*.dll") do (
    if exist "%%~fF" (
        echo Unsupported obsolete root-level DLL: %%~fF
        echo Remove it manually and use the canonical build\bin layout.
        exit /b 1
    )
)

set CMAKE_ARGS=-S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if defined NINJA_EXE (
    set CMAKE_ARGS=!CMAKE_ARGS! -DCMAKE_MAKE_PROGRAM=%NINJA_EXE%
)
if defined VCPKG_ROOT (
    set CMAKE_ARGS=!CMAKE_ARGS! -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
    if not defined VCPKG_DEFAULT_TRIPLET (
        set VCPKG_TARGET_TRIPLET_NAME=x64-mingw-dynamic
    ) else (
        set VCPKG_TARGET_TRIPLET_NAME=%VCPKG_DEFAULT_TRIPLET%
    )
    set CMAKE_ARGS=!CMAKE_ARGS! -DVCPKG_TARGET_TRIPLET=!VCPKG_TARGET_TRIPLET_NAME! -DCMAKE_PREFIX_PATH=%VCPKG_ROOT%\installed\!VCPKG_TARGET_TRIPLET_NAME!
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

if not exist "build\bin\maploader.dll" (
    echo maploader.dll was not generated in build\bin.
    exit /b 1
)
if not exist "build\bin\model_loader.dll" (
    echo model_loader.dll was not generated in build\bin.
    exit /b 1
)
if not exist "build\settings\" mkdir "build\settings"

for %%F in (LICENSE NOTICE THIRD_PARTY_NOTICES.md) do (
    if exist "%%F" copy /y "%%F" "build\%%F" >nul
)

echo kobushiCPP dev built: %cd%\build
echo Runtime DLLs: %cd%\build\bin
echo Settings: %cd%\build\settings

rem --- Build completion notification ---
powershell -Command "if (Get-Module -ListAvailable -Name BurntToast) { exit 0 } else { exit 1 }" >nul 2>&1
if errorlevel 1 (
    msg %USERNAME% /time:10 "build finished" >nul 2>&1
) else (
    powershell -Command "New-BurntToastNotification -Text 'Build finished'"
)

pause
