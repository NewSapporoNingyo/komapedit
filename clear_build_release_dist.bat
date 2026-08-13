@echo off
setlocal
cd /d "%~dp0"

set "TARGET=%CD%\build_release"

if not exist "%TARGET%\" (
    echo build_release directory was not found: %TARGET%
    exit /b 1
)

for %%F in (settings.ini history.ini imgui.ini) do (
    if exist "%TARGET%\%%F" (
        echo Unsupported obsolete settings file: %TARGET%\%%F
        echo Remove it manually and use the canonical settings layout.
        exit /b 1
    )
)
for %%F in ("%TARGET%\*.dll") do (
    if exist "%%~fF" (
        echo Unsupported obsolete root-level DLL: %%~fF
        echo Remove it manually and use the canonical bin layout.
        exit /b 1
    )
)

if not exist "%TARGET%\komapedit.exe" (
    echo komapedit.exe was not found: %TARGET%\komapedit.exe
    exit /b 1
)
if not exist "%TARGET%\bin\maploader.dll" (
    echo maploader.dll was not found: %TARGET%\bin\maploader.dll
    exit /b 1
)
if not exist "%TARGET%\bin\model_loader.dll" (
    echo model_loader.dll was not found: %TARGET%\bin\model_loader.dll
    exit /b 1
)
if not exist "%TARGET%\settings\" mkdir "%TARGET%\settings"

for %%F in (LICENSE NOTICE THIRD_PARTY_NOTICES.md) do (
    if exist "%%F" copy /y "%%F" "%TARGET%\%%F" >nul
)

for /f "delims=" %%F in ('dir /b /a-d "%TARGET%" 2^>nul') do (
    set "KEEP="
    if /I "%%F"=="komapedit.exe" set "KEEP=1"
    if /I "%%F"=="LICENSE" set "KEEP=1"
    if /I "%%F"=="NOTICE" set "KEEP=1"
    if /I "%%F"=="THIRD_PARTY_NOTICES.md" set "KEEP=1"
    if not defined KEEP del /f /q "%TARGET%\%%F"
)

for /f "delims=" %%F in ('dir /b /a-d "%TARGET%\bin" 2^>nul') do (
    if /I not "%%~xF"==".dll" del /f /q "%TARGET%\bin\%%F"
)

for /f "delims=" %%D in ('dir /b /ad "%TARGET%\bin" 2^>nul') do (
    rmdir /s /q "%TARGET%\bin\%%D"
)

for /f "delims=" %%D in ('dir /b /ad "%TARGET%" 2^>nul') do (
    set "KEEP="
    if /I "%%D"=="bin" set "KEEP=1"
    if /I "%%D"=="settings" set "KEEP=1"
    if not defined KEEP rmdir /s /q "%TARGET%\%%D"
)

echo build_release cleaned for distribution.
echo Kept: komapedit.exe, bin\*.dll, settings, LICENSE, NOTICE, THIRD_PARTY_NOTICES.md

pause
