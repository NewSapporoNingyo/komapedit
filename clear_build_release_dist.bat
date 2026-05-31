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
    set "KEEP="
    if /I "%%F"=="komapedit.exe" set "KEEP=1"
    if /I "%%~xF"==".dll" set "KEEP=1"
    if /I "%%F"=="LICENSE" set "KEEP=1"
    if /I "%%F"=="NOTICE" set "KEEP=1"
    if /I "%%F"=="THIRD_PARTY_NOTICES.md" set "KEEP=1"
    if not defined KEEP del /f /q "%TARGET%\%%F"
)

for /f "delims=" %%D in ('dir /b /ad "%TARGET%"') do (
    rmdir /s /q "%TARGET%\%%D"
)

echo build_release cleaned for distribution.
echo Kept: komapedit.exe, all DLL files, LICENSE, NOTICE, THIRD_PARTY_NOTICES.md

pause
