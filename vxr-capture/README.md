# vxr-capture: High-Performance DXGI Desktop Duplication Module

`vxr-capture` is the native C++ capture engine for Project VXR on Windows. It interfaces directly with Direct3D 11 and DXGI 1.2 Desktop Duplication to capture desktop frames with zero CPU overhead, sharing textures directly across GPU pipelines.

---

## Architectural Features

1. **Direct3D 11 & DXGI 1.2 Output Duplication**:
   - Enumerates physical adapters (NVIDIA GeForce, Intel UHD, AMD Radeon) and virtual displays (IDDCX).
   - Duplicates desktop output via `IDXGIOutput1::DuplicateOutput`.
   - Handles multi-monitor setups, display rotation, and custom refresh rates (60Hz, 72Hz, 90Hz, 120Hz, 165Hz).

2. **Zero-Copy VRAM Pipeline**:
   - Creates a shared DirectX texture (`D3D11_RESOURCE_MISC_SHARED`) in `DXGI_FORMAT_B8G8R8A8_UNORM`.
   - Transfers captured frames directly GPU-to-GPU via `ID3D11DeviceContext::CopyResource`.
   - Exposes Windows shared NT handle (`HANDLE`) for direct zero-copy consumption by NVENC hardware encoders or Vulkan runtimes.

3. **0ms Hardware Cursor Tracking**:
   - Extracts cursor visibility, coordinates $(X, Y)$, hotspot, and shape type directly from `DXGI_OUTDUPL_FRAME_INFO`.
   - Feeds immediate cursor positions to the network stream for client-side local rendering at 0ms latency.

4. **Robust Error Recovery**:
   - Detects `DXGI_ERROR_ACCESS_LOST` (triggered by UAC prompts, display resolution changes, or fullscreen game mode switches).
   - Detects `DXGI_ERROR_DEVICE_RESET` / `DXGI_ERROR_DEVICE_REMOVED`.
   - Provides clean re-initialization API (`vxr_capture_reinit`) to recover without restarting the streamer process.

---

## Building and Testing

Prerequisites:
- Windows 10/11
- Visual Studio 2019/2022 (MSVC) or Build Tools with Windows 10 SDK
- CMake 3.15+

### Build
```powershell
cmake -S vxr-capture -B vxr-capture/build -G "Visual Studio 16 2019" -A x64
cmake --build vxr-capture/build --config Release
```

### Run Tests
```powershell
.\vxr-capture\build\Release\vxr_capture_test.exe
```

---

## C API Reference (`vxr_capture.h`)

```c
VxrCaptureHandle vxr_capture_create(void);
VxrCaptureStatus vxr_capture_init(VxrCaptureHandle handle, uint32_t adapter_index, uint32_t output_index);
VxrCaptureStatus vxr_capture_get_desc(VxrCaptureHandle handle, VxrCaptureDesc* out_desc);
VxrCaptureStatus vxr_capture_acquire_frame(VxrCaptureHandle handle, uint32_t timeout_ms, VxrCapturedFrame* out_frame);
VxrCaptureStatus vxr_capture_release_frame(VxrCaptureHandle handle);
VxrCaptureStatus vxr_capture_reinit(VxrCaptureHandle handle);
void vxr_capture_destroy(VxrCaptureHandle handle);
```
