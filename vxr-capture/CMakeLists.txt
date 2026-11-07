cmake_minimum_required(VERSION 3.15)
project(vxr_capture VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core capture library
add_library(vxr_capture STATIC
    src/DxgiDuplicator.cpp
    src/vxr_capture.cpp
)

target_include_directories(vxr_capture PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(vxr_capture PUBLIC
    d3d11
    dxgi
)

# Test executable
add_executable(vxr_capture_test
    tests/test_capture.cpp
)

target_link_libraries(vxr_capture_test PRIVATE
    vxr_capture
    d3d11
    dxgi
)
