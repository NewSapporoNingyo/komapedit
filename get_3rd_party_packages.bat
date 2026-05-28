if not exist "%~dp0third_party\" (
    mkdir "%~dp0third_party"
)

cd /d "%~dp0third_party"
git clone --branch docking https://github.com/ocornut/imgui.git
git clone https://github.com/epezent/implot.git
