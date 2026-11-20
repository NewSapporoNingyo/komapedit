@echo off
if not defined VCPKG_ROOT (
    echo Define VCPKG_ROOT as the vcpkg installation directory. 1>&2
    exit /b 1
)
"%VCPKG_ROOT%\vcpkg.exe" install assimp:x64-mingw-dynamic
exit /b %errorlevel%
