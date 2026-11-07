cmake_minimum_required(VERSION 3.15)
project(vxr_encoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(ENCODER_SOURCES
    src/VxrNvencEncoder.cpp
    src/VxrAmfEncoder.cpp
    src/VxrVideoEncoder.cpp
    src/vxr_encoder.cpp
)

# Core Encoder Library
add_library(vxr_encoder SHARED
    ${ENCODER_SOURCES}
)

target_compile_definitions(vxr_encoder PRIVATE
    VXR_ENCODER_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(vxr_encoder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Test Executable
add_executable(test_encoder
    tests/test_encoder.cpp
    ${ENCODER_SOURCES}
)

target_compile_definitions(test_encoder PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(test_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
