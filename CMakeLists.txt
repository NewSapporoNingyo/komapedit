cmake_minimum_required(VERSION 3.20)
project(kobushi_maploader LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

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

find_package(assimp CONFIG REQUIRED)

add_library(model_loader SHARED
    src/model_loader.cpp
)

target_compile_features(model_loader PRIVATE cxx_std_17)
target_include_directories(model_loader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(model_loader PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(model_loader PRIVATE assimp::assimp)
set_target_properties(model_loader PROPERTIES
    OUTPUT_NAME "model_loader"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
if(WIN32)
    set(KOMAPEDIT_MODEL_RUNTIME_DLL_DIR "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(KOMAPEDIT_MODEL_RUNTIME_DLL_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
        else()
            set(KOMAPEDIT_MODEL_RUNTIME_DLL_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
    elseif(CMAKE_PREFIX_PATH)
        foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
            if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND EXISTS "${prefix}/debug/bin")
                set(KOMAPEDIT_MODEL_RUNTIME_DLL_DIR "${prefix}/debug/bin")
                break()
            elseif(EXISTS "${prefix}/bin")
                set(KOMAPEDIT_MODEL_RUNTIME_DLL_DIR "${prefix}/bin")
                break()
            endif()
        endforeach()
    endif()

    if(KOMAPEDIT_MODEL_RUNTIME_DLL_DIR AND EXISTS "${KOMAPEDIT_MODEL_RUNTIME_DLL_DIR}")
        file(GLOB KOMAPEDIT_MODEL_RUNTIME_DLLS "${KOMAPEDIT_MODEL_RUNTIME_DLL_DIR}/*.dll")
        if(KOMAPEDIT_MODEL_RUNTIME_DLLS)
            add_custom_command(TARGET model_loader POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${KOMAPEDIT_MODEL_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:model_loader>
                COMMAND_EXPAND_LISTS
            )
        endif()
    elseif(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
        add_custom_command(TARGET model_loader POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:model_loader>
                $<TARGET_FILE_DIR:model_loader>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

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
    src/gui_kme.cpp
    src/canvas2D.cpp
    src/canvas3D.cpp
    src/datatable.cpp
)
add_dependencies(komapedit model_loader)
set_target_properties(komapedit PROPERTIES WIN32_EXECUTABLE TRUE)
if(WIN32)
    target_sources(komapedit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/komapedit.rc
    )
endif()
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
