cmake_minimum_required(VERSION 3.15)
project(vxr_quest_client VERSION 0.1.0 LANGUAGES C CXX)

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
    src/VxrVrMenuUi.cpp
    src/VxrClientNetwork.cpp
    src/VxrQuestApp.cpp
    src/main.cpp
)

if(ANDROID)
    # Android NDK NativeActivity build with Khronos OpenXR Loader & EGL/GLES3
    include_directories(
        ${ANDROID_NDK}/sources/android/native_app_glue
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr/prefab/modules/headers/include
    )

    add_library(vxr_quest_client SHARED
        ${CLIENT_SOURCES}
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )

    target_compile_definitions(vxr_quest_client PRIVATE
        ANDROID
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
    )

    target_link_libraries(vxr_quest_client PUBLIC
        android
        log
        mediandk
        vulkan
        EGL
        GLESv3
        dl
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openxr/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so
    )

else()
    # Windows Host Validation & Test Targets
    add_library(vxr_quest_client SHARED
        ${CLIENT_SOURCES}
    )

    target_compile_definitions(vxr_quest_client PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_libraries(vxr_quest_client PRIVATE
        ws2_32
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
        src/VxrVrMenuUi.cpp
        src/VxrClientNetwork.cpp
        src/VxrQuestApp.cpp
    )

    target_include_directories(test_quest_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_definitions(test_quest_client PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )

    target_link_libraries(test_quest_client PRIVATE
        ws2_32
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
endif()
