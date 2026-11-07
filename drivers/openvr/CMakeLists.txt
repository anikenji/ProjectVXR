cmake_minimum_required(VERSION 3.15)
project(vxr_openvr_driver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(DRIVER_SOURCES
    src/VxrHmdDevice.cpp
    src/VxrControllerDevice.cpp
    src/VxrDriverServerProvider.cpp
    src/VxrDriverFactory.cpp
    src/VxrDriverApi.cpp
)

# Build driver_vrserver.dll
add_library(driver_vrserver SHARED
    ${DRIVER_SOURCES}
)

target_compile_definitions(driver_vrserver PRIVATE
    VXR_OPENVR_DRIVER_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(driver_vrserver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Standard SteamVR directory layout: bin/win64/driver_vrserver.dll
set_target_properties(driver_vrserver PROPERTIES
    OUTPUT_NAME "driver_vrserver"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/win64"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin/win64"
)

# Test executable
add_executable(test_openvr_driver
    tests/test_openvr_driver.cpp
    ${DRIVER_SOURCES}
)

target_compile_definitions(test_openvr_driver PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(test_openvr_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
