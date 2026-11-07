# Project VXR: Meta Quest Client (Pure C++ NDK + Vulkan 1.3 + OpenXR)

`quest-client` is the native Android NDK client application running on standalone VR headsets (Meta Quest 2, Quest 3, Quest Pro).

---

## Architectural Features

1. **Android NDK NativeActivity**:
   - Zero Java/JNI runtime overhead on the render and decode loops.
   - Entry point configured via `android_main` in `main.cpp` and declared in `AndroidManifest.xml`.

2. **AMediaCodec Low-Latency Video Pipeline (`VxrMediaCodecDecoder`)**:
   - Explicitly configured with `KEY_LOW_LATENCY` (`"low-latency" = 1`, Android 11+ API 30).
   - Real-time stream priority (`"priority" = 0`).
   - Qualcomm Snapdragon XR2 hardware acceleration flags (`"vendor.qti-ext-dec-low-latency.enable" = 1` and immediate presentation order `"vendor.qti-ext-dec-picture-order.enable" = 0`).
   - Supports H.264, HEVC 10-bit (P010), and AV1 decoding.

3. **Zero-Copy AHardwareBuffer to Vulkan 1.3 Texture Binding (`VxrVulkanRenderer`)**:
   - Decoded video frames sitting in GPU physical memory are retrieved as `AHardwareBuffer*`.
   - Imported directly into Vulkan using `VK_ANDROID_external_memory_android_hardware_buffer` and `VkImportAndroidHardwareBufferInfoANDROID`.
   - Binds directly to a `VkImage` sampled via YCbCr conversion sampler (`VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`).
   - **0 CPU copies, 0 memory transfers** between decoder and VR display compositor.

4. **0ms Local Hardware Cursor Plane (`VxrHardwareCursor`)**:
   - Desktop mouse cursor coordinates $(X, Y)$ are delivered via lightweight UDP packets and projected locally as a 3D overlay quad billboard.
   - Rendered at the native headset display refresh rate (72Hz, 90Hz, 120Hz) with 2mm Z-offset in front of the virtual desktop, eliminating perceived mouse latency.

5. **On-Device Synchronous Spacewarp (SSW) (`quest-client/shaders/ssw_reproject.comp`)**:
   - Compute shader pipeline that reprojects dropped or late video frames on the Quest GPU using motion vectors and head tracking rotational delta.
   - Preserves 90Hz/120Hz display smoothness without motion judder during Wi-Fi packet jitter.

6. **Post-Processing Sharpening Compute Shaders**:
   - **AMD FidelityFX CAS (`quest-client/shaders/ffx_cas.comp`)**: 5-tap adaptive contrast-directed filter that removes video compression blur without haloing or ringing.
   - **Qualcomm SGSR (`quest-client/shaders/qcom_sgsr.comp`)**: Edge-directed spatial enhancement shader optimized for Qualcomm Adreno 740 GPUs (Snapdragon XR2 Gen 2).

7. **Meta OpenXR Mobile Integration (`VxrOpenXrClient`)**:
   - Full OpenXR session lifecycle management (`IDLE`, `READY`, `SYNCHRONIZED`, `VISIBLE`, `FOCUSED`, `STOPPING`).
   - 6DoF head tracking with IPD calculations (Left eye: $-31.5\text{mm}$, Right eye: $+31.5\text{mm}$) and tangent FOV projections.
   - Synchronous SpaceWarp (SSW) motion vector generation and Fixed Foveated Rendering (FFR) support.

8. **Unified Client Coordinator (`VxrQuestApp`)**:
   - Connects the decode, zero-copy import, cursor overlay, SSW reprojection, and OpenXR frame submission loop into an optimized 90Hz/120Hz pipeline.

---

## Building and Testing

### Build with CMake / MSVC (Host validation)
```powershell
cmake -S quest-client -B quest-client/build -G "Visual Studio 16 2019" -A x64
cmake --build quest-client/build --config Release
.\quest-client\build\Release\test_quest_client.exe
.\quest-client\build\Release\test_shaders.exe
```

### Build with Android NDK (Quest APK deployment)
```bash
$ANDROID_NDK/ndk-build -C quest-client
# Or via CMake:
cmake -S quest-client -B quest-client/build-android \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29
```
