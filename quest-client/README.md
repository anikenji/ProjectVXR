# Project VXR - Meta Quest Client (C++ / NDK)

This directory contains the native Android / Meta Quest VR client application:

- **NativeActivity & NDK Core**: Pure C++ execution model minimizing Java/JNI overhead on the render thread.
- **Zero-Copy Video Pipeline**: Hardware decoding via `AMediaCodec` directly exported to Vulkan textures through `AHardwareBuffer`.
- **OpenXR Mobile Integration**: 6DoF head & controller tracking, Synchronous SpaceWarp (SSW), and Fixed Foveated Rendering (FFR via `XR_FB_foveation`).
- **Post-Processing Shaders**: FidelityFX CAS & Qualcomm SGSR sharpening passes running on Adreno GPU.
- **0ms Hardware Cursor**: Local client-side cursor projection for zero-latency desktop interaction.
