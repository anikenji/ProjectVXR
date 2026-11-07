# Project VXR: IDDCX Virtual Display Driver (UMDF v2)

`drivers/iddcx` is the native Windows Indirect Display Driver (IDDCX) subsystem for Project VXR. It allows creating virtual headless displays on Windows without physical monitors or HDMI dummy plugs, streaming desktop video directly to standalone VR headsets.

---

## Architectural Features

1. **UMDF v2 Indirect Display Driver Architecture**:
   - Implements user-mode driver framework architecture (`IddCx`) defined by Microsoft for Windows 10/11.
   - Declared via `VxrIddCx.inf` under display class (`ClassGuid = {4D36E968-E325-11CE-BFC1-08002BE10318}`) using `IndirectKmd` upper filter.

2. **Multi-Monitor Headless Support (1 to 3 Monitors)**:
   - Dynamic creation, enumeration, and removal of up to 3 concurrent virtual displays simultaneously.
   - Enables immersive multi-monitor virtual desktop arrangements inside VR.

3. **High Resolutions & VR-Native Refresh Rates**:
   - **Supported Resolutions**: 1920x1080 (FHD), 2560x1440 (QHD), 3840x2160 (4K UHD).
   - **Supported Refresh Rates**: 60Hz, 72Hz, 90Hz, 120Hz.
   - Dynamic mode switching: seamless in-flight resolution and refresh rate adaptation.

4. **Dynamic VESA EDID Synthesis (`VxrEdidSynthesizer`)**:
   - Synthesizes compliant 128-byte VESA EDID binary blocks on-the-fly.
   - Standard VESA header (`00 FF FF FF FF FF FF 00`), compressed ASCII manufacturer ID (`VXR`), product IDs, 10-bit digital display flags, display gamma (2.20), sRGB chromaticity coordinates, established timings, standard timing identifiers, and Detailed Timing Descriptors (DTD).
   - Dynamic modulo-256 checksum generation guaranteeing OS validation pass.

5. **DirectX Swapchain Buffer Acquisition & Zero-Copy Sharing**:
   - DWM renders desktop frames directly into virtual swapchain buffers.
   - Provides `ID3D11Texture2D*` and Windows shared NT handles (`HANDLE`) for direct, zero-copy ingestion by `vxr-capture` and hardware NVENC encoders.

6. **User-Mode Control API (`vxr_iddcx_control.h`)**:
   - Thread-safe C FFI interface enabling `vxr-server` and external tools to create, reconfigure, query, and destroy virtual monitors programmatically.

---

## C API Reference (`vxr_iddcx_control.h`)

```c
uint32_t VxrIdd_CreateMonitor(uint32_t width, uint32_t height, uint32_t refresh_rate);
bool VxrIdd_DestroyMonitor(uint32_t monitor_id);
bool VxrIdd_SetResolution(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate);
uint32_t VxrIdd_GetMonitorCount(void);
bool VxrIdd_GetMonitorInfo(uint32_t monitor_id, VxrMonitorDesc* out_desc);
bool VxrIdd_AcquireNextFrame(uint32_t monitor_id, uint32_t timeout_ms, VxrIddFrame* out_frame);
bool VxrIdd_ReleaseFrame(uint32_t monitor_id);
bool VxrIdd_SynthesizeEdid(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate, uint8_t out_edid[128]);
bool VxrIdd_ValidateEdid(const uint8_t edid[128]);
```

---

## Build & Test

```powershell
cmake -S drivers/iddcx -B drivers/iddcx/build -G "Visual Studio 16 2019" -A x64
cmake --build drivers/iddcx/build --config Release
.\drivers\iddcx\build\Release\test_iddcx.exe
```
