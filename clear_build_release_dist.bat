@echo off
setlocal
cd /d "%~dp0"

set "TARGET=%CD%\build_release"

if not exist "%TARGET%\" (
    echo build_release directory was not found: %TARGET%
    exit /b 1
)

for %%F in (LICENSE NOTICE THIRD_PARTY_NOTICES.md) do (
    if exist "%%F" copy /y "%%F" "%TARGET%\%%F" >nul
)

for /f "delims=" %%F in ('dir /b /a-d "%TARGET%"') do (
    if /I not "%%F"=="komapedit.exe" if /I not "%%F"=="maploader.dll" if /I not "%%F"=="LICENSE" if /I not "%%F"=="NOTICE" if /I not "%%F"=="THIRD_PARTY_NOTICES.md" (
        del /f /q "%TARGET%\%%F"
    )
)

for /f "delims=" %%D in ('dir /b /ad "%TARGET%"') do (
    rmdir /s /q "%TARGET%\%%D"
)

echo build_release cleaned for distribution.
echo Kept: komapedit.exe, maploader.dll, LICENSE, NOTICE, THIRD_PARTY_NOTICES.md

pause
