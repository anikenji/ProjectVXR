cmake_minimum_required(VERSION 3.15)
project(vxr_streamer VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ProjectVXR WIN32
    src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../quest-client/src/VxrComputeShaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../vxr-capture/src/DxgiDuplicator.cpp
)

target_compile_options(ProjectVXR PRIVATE /utf-8)

target_include_directories(ProjectVXR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../vxr-capture/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../vxr-encoder/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/iddcx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../quest-client/include
)

target_compile_definitions(ProjectVXR PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(ProjectVXR PRIVATE
    user32
    gdi32
    gdiplus
    shell32
    shlwapi
    advapi32
    d3d11
    dxgi
    ole32
    uuid
    dwmapi
    ws2_32
    iphlpapi
    avrt
)
