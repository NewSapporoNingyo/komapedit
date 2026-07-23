@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

set CMAKE_ARGS=-S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release
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

cmake --build build_release --config Release
if errorlevel 1 (
    echo cmake build failed.
    exit /b 1
)

if not exist "build_release\bin\maploader.dll" (
    echo maploader.dll was not generated in build_release\bin.
    exit /b 1
)
if not exist "build_release\bin\model_loader.dll" (
    echo model_loader.dll was not generated in build_release\bin.
    exit /b 1
)
if not exist "build_release\settings\" mkdir "build_release\settings"

rem Migrate legacy root-level INI files without overwriting newer settings.
for %%F in (settings.ini history.ini imgui.ini) do (
    if exist "build_release\%%F" (
        if exist "build_release\settings\%%F" (
            echo Conflicting settings files: build_release\%%F and build_release\settings\%%F
            exit /b 1
        )
        move /y "build_release\%%F" "build_release\settings\%%F" >nul
        if errorlevel 1 (
            echo Failed to migrate build_release\%%F to build_release\settings.
            exit /b 1
        )
    )
)

rem Remove obsolete root-level DLLs left by builds made before the runtime layout change.
for %%F in ("build_release\*.dll") do (
    if exist "%%~fF" del /f /q "%%~fF"
)

for %%F in (LICENSE NOTICE THIRD_PARTY_NOTICES.md) do (
    if exist "%%F" copy /y "%%F" "build_release\%%F" >nul
)

echo kobushiCPP release built: %cd%\build_release
echo Runtime DLLs: %cd%\build_release\bin
echo Settings: %cd%\build_release\settings

rem --- Build completion notification ---
powershell -Command "if (Get-Module -ListAvailable -Name BurntToast) { exit 0 } else { exit 1 }" >nul 2>&1
if errorlevel 1 (
    msg %USERNAME% /time:10 "build finished" >nul 2>&1
) else (
    powershell -Command "New-BurntToastNotification -Text 'Build finished'"
)

pause
