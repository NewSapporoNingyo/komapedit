cmake_minimum_required(VERSION 3.20)
project(kobushi_maploader LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

option(KOMAPEDIT_STRICT_WARNINGS
    "Treat high-value warnings in komapedit-owned targets as errors"
    OFF
)

function(komapedit_enable_strict_warnings target)
    if(NOT KOMAPEDIT_STRICT_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wformat=2
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
            -Wnull-dereference
            -Wshadow=local
            -Wredundant-decls
            -Werror
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wformat=2
            -Wshadow
            -Werror
        )
    endif()
endfunction()

set(KOMAPEDIT_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(KOMAPEDIT_DLL_OUTPUT_DIRECTORY "${KOMAPEDIT_RUNTIME_OUTPUT_DIRECTORY}/bin")
set(KOMAPEDIT_SETTINGS_OUTPUT_DIRECTORY "${KOMAPEDIT_RUNTIME_OUTPUT_DIRECTORY}/settings")
file(MAKE_DIRECTORY
    "${KOMAPEDIT_DLL_OUTPUT_DIRECTORY}"
    "${KOMAPEDIT_SETTINGS_OUTPUT_DIRECTORY}"
)

add_library(kme_json STATIC
    src/common/json.cpp
)
target_compile_features(kme_json PRIVATE cxx_std_17)
target_include_directories(kme_json PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
set_target_properties(kme_json PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BUILD_TESTING)
    add_executable(kme_json_tests
        src/common/tests/json_tests.cpp
    )
    target_compile_features(kme_json_tests PRIVATE cxx_std_17)
    target_link_libraries(kme_json_tests PRIVATE kme_json)
    add_test(NAME kme_json_tests COMMAND kme_json_tests)
endif()

add_library(kme_text_decoder STATIC
    src/maploader/text_decoder.cpp
)
target_compile_features(kme_text_decoder PRIVATE cxx_std_17)
target_include_directories(kme_text_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/maploader
)
set_target_properties(kme_text_decoder PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_compile_definitions(kme_text_decoder PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

add_library(maploader SHARED
    src/maploader/maploader.cpp
    src/maploader/maploader_core.cpp
    src/maploader/maploader_parser.cpp
    src/maploader/maploader_geometry.cpp
    src/maploader/maploader_ir_json.cpp
    src/maploader/maploader_edits.cpp
    src/maploader/diagnostics.cpp
    src/maploader/c_api.cpp
)

target_compile_features(maploader PRIVATE cxx_std_17)
target_include_directories(maploader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(maploader PRIVATE kme_json kme_text_decoder)

if(MINGW)
    target_link_libraries(maploader PRIVATE ole32)
endif()

set_target_properties(maploader PROPERTIES
    OUTPUT_NAME "maploader"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${KOMAPEDIT_DLL_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${KOMAPEDIT_DLL_OUTPUT_DIRECTORY}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

find_package(assimp CONFIG REQUIRED)

add_library(model_loader SHARED
    src/model_loader/model_loader.cpp
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
    RUNTIME_OUTPUT_DIRECTORY "${KOMAPEDIT_DLL_OUTPUT_DIRECTORY}"
    LIBRARY_OUTPUT_DIRECTORY "${KOMAPEDIT_DLL_OUTPUT_DIRECTORY}"
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
    src/main_window/gui_kme.cpp
    src/main_window/app_settings.cpp
    src/main_window/runtime_paths.cpp
    src/main_window/maploader_runtime.cpp
    src/main_window/file_structure_diagram.cpp
    src/main_window/text_preview.cpp
    src/main_window/debug_headless.cpp
    src/main_window/touch_input.cpp
    src/canvas2d/canvas2D.cpp
    src/canvas2d/profile_plots.cpp
    src/canvas3d/canvas3D.cpp
    src/table/datatable.cpp
    src/table/table_navigation.cpp
)
add_dependencies(komapedit maploader model_loader)
set_target_properties(komapedit PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${KOMAPEDIT_RUNTIME_OUTPUT_DIRECTORY}"
)
if(WIN32)
    target_sources(komapedit PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/komapedit.rc
    )
endif()
target_include_directories(komapedit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main_window
)
target_compile_definitions(komapedit PRIVATE
    MAPLOADER_RUNTIME_DISPATCH
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_link_options(komapedit PRIVATE /ENTRY:mainCRTStartup)
endif()
target_link_libraries(komapedit PRIVATE
    kme_json
    kme_text_decoder
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

foreach(komapedit_owned_target IN ITEMS
        kme_json
        kme_json_tests
        kme_text_decoder
        maploader
        model_loader
        komapedit)
    if(TARGET ${komapedit_owned_target})
        komapedit_enable_strict_warnings(${komapedit_owned_target})
    endif()
endforeach()
