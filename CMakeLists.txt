cmake_minimum_required(VERSION 3.20)
project(kobushi_maploader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(maploader SHARED
    src/maploader.cpp
)

target_compile_features(maploader PRIVATE cxx_std_17)
target_include_directories(maploader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MINGW)
    target_link_libraries(maploader PRIVATE ole32)
endif()

set_target_properties(maploader PROPERTIES
    OUTPUT_NAME "maploader"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot")

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/cpp
)
target_compile_definitions(imgui PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_library(implot STATIC
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)
target_include_directories(implot PUBLIC ${IMPLOT_DIR})
target_link_libraries(implot PUBLIC imgui)

add_executable(komapedit
    src/gui_app.cpp
)
set_target_properties(komapedit PROPERTIES WIN32_EXECUTABLE TRUE)
target_include_directories(komapedit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(komapedit PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_link_options(komapedit PRIVATE /ENTRY:mainCRTStartup)
endif()
target_link_libraries(komapedit PRIVATE
    maploader
    implot
    imgui
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    ole32
    uuid
    windowscodecs
    comdlg32
    shell32
    gdi32
)
