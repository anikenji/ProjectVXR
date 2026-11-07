cmake_minimum_required(VERSION 3.15)
project(VxrAudioCable VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(VxrAudioCable SHARED
    src/VxrAudioCable.cpp
)

target_compile_definitions(VxrAudioCable PRIVATE
    VXR_AUDIO_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)

target_include_directories(VxrAudioCable PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
