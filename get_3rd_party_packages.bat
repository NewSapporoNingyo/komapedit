if not exist "%~dp0third_party\" (
    mkdir "%~dp0third_party"
    if errorlevel 1 exit /b 1
)

cd /d "%~dp0third_party" || exit /b 1
git clone --branch docking https://github.com/ocornut/imgui.git
if errorlevel 1 exit /b %errorlevel%
git clone https://github.com/epezent/implot.git
exit /b %errorlevel%
