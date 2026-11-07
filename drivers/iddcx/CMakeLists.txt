cmake_minimum_required(VERSION 3.15)
project(vxr_iddcx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(IDDCX_SOURCES
    src/VxrEdidSynthesizer.cpp
    src/VxrIddMonitor.cpp
    src/VxrIddAdapter.cpp
    src/vxr_iddcx_control.cpp
    src/DriverEntry.cpp
)

# Driver DLL
add_library(VxrIddCx SHARED
    ${IDDCX_SOURCES}
)

target_compile_definitions(VxrIddCx PRIVATE
    VXR_IDDCX_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(VxrIddCx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Test Executable
add_executable(test_iddcx
    tests/test_iddcx.cpp
    src/VxrEdidSynthesizer.cpp
    src/VxrIddMonitor.cpp
    src/VxrIddAdapter.cpp
    src/vxr_iddcx_control.cpp
)

target_compile_definitions(test_iddcx PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(test_iddcx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
