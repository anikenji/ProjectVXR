cmake_minimum_required(VERSION 3.15)
project(vxr_openxr_runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(RUNTIME_SOURCES
    src/VxrXrNegotiation.cpp
    src/VxrXrRuntime.cpp
    src/VxrXrSession.cpp
    src/VxrXrSwapchain.cpp
    src/VxrXrSpaces.cpp
)

# OpenXR Runtime DLL: openxr_vxr.dll
add_library(openxr_vxr SHARED
    ${RUNTIME_SOURCES}
)

target_compile_definitions(openxr_vxr PRIVATE
    VXR_OPENXR_RUNTIME_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(openxr_vxr PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Runtime Switcher CLI
add_executable(vxr_xr_switcher
    switcher/vxr_xr_switcher.cpp
)

target_compile_definitions(vxr_xr_switcher PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# Test Executable
add_executable(test_openxr_runtime
    tests/test_openxr_runtime.cpp
    ${RUNTIME_SOURCES}
)

target_compile_definitions(test_openxr_runtime PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(test_openxr_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
