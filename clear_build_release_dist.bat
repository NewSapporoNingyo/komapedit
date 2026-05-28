@echo off
setlocal
cd /d "%~dp0"

set "TARGET=%CD%\build_release"

if not exist "%TARGET%\" (
    echo build_release directory was not found: %TARGET%
    exit /b 1
)

for /f "delims=" %%F in ('dir /b /a-d "%TARGET%"') do (
    if /I not "%%F"=="komapedit.exe" if /I not "%%F"=="maploader.dll" (
        del /f /q "%TARGET%\%%F"
    )
)

for /f "delims=" %%D in ('dir /b /ad "%TARGET%"') do (
    rmdir /s /q "%TARGET%\%%D"
)

echo build_release cleaned for distribution.
echo Kept: kobushi_gui.exe, maploader.dll

pause
