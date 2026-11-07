cmake_minimum_required(VERSION 3.15)
project(vxr_quest_client VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(CLIENT_SOURCES
    src/VxrMediaCodecDecoder.cpp
    src/VxrVulkanRenderer.cpp
    src/VxrHardwareCursor.cpp
    src/VxrOpenXrClient.cpp
    src/VxrComputeShaders.cpp
    src/spatial_canvas.cpp
    src/VxrQuestApp.cpp
    src/main.cpp
)

# Core Quest Client Library
add_library(vxr_quest_client SHARED
    ${CLIENT_SOURCES}
)

target_include_directories(vxr_quest_client PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(vxr_quest_client PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# Client Pipeline Test Executable
add_executable(test_quest_client
    tests/test_quest_client.cpp
    src/VxrMediaCodecDecoder.cpp
    src/VxrVulkanRenderer.cpp
    src/VxrHardwareCursor.cpp
    src/VxrOpenXrClient.cpp
    src/VxrComputeShaders.cpp
    src/spatial_canvas.cpp
    src/VxrQuestApp.cpp
)

target_include_directories(test_quest_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(test_quest_client PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# Shaders & Reprojection Test Executable
add_executable(test_shaders
    tests/test_shaders.cpp
    src/VxrComputeShaders.cpp
)

target_include_directories(test_shaders PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(test_shaders PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# Spatial Canvas & Passthrough Cutout Test Executable
add_executable(test_spatial_canvas
    tests/test_spatial_canvas.cpp
    src/spatial_canvas.cpp
)

target_include_directories(test_spatial_canvas PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(test_spatial_canvas PRIVATE
    _CRT_SECURE_NO_WARNINGS
)
