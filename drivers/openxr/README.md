# Project VXR: VXR-XR Native OpenXR Runtime (openxr_vxr.dll)

`drivers/openxr` is the native Khronos OpenXR 1.0/1.1 runtime subsystem for Project VXR. It allows standalone and PC flight/racing simulation titles (such as Microsoft Flight Simulator 2020/2024, DCS World, Assetto Corsa Competizione, and iRacing) to run directly on Meta Quest headsets **without passing through SteamVR or the Oculus desktop software**, eliminating latency and frame judder.

---

## Architectural Features

1. **Khronos OpenXR 1.0/1.1 Loader Negotiation**:
   - Implements `xrNegotiateLoaderRuntimeInterface` and `xrGetInstanceProcAddr` conforming to the Khronos OpenXR loader-runtime specification.
   - Declared to the operating system via the JSON runtime manifest (`openxr_vxr.json`).

2. **SteamVR Bypass Pipeline**:
   - Connects games directly to the Project VXR streaming pipeline.
   - Eliminates SteamVR's intermediate compositor layer, secondary tracking reprojection pass, and inter-process frame copies.

3. **Direct GPU Swapchain Shared Handle Extraction**:
   - Generates D3D11 / D3D12 triple-buffered swapchains matching native Quest 3 resolution (2064x2208 per eye = 4128x2208).
   - Exposes Windows NT shared texture handles (`HANDLE`) on each frame submission (`xrEndFrame`) for zero-copy handoff directly to `vxr-capture` and hardware NVENC video encoders in VRAM.

4. **Predictive Frame Loop & View Locating**:
   - `xrWaitFrame`: Predicts display time at native 90Hz / 120Hz display frequencies.
   - `xrLocateViews`: Produces camera projection transforms for Left and Right eyes with 63mm IPD ($-31.5\text{mm}$ and $+31.5\text{mm}$) and symmetrical tangent FOVs ($sim 100^\circ$).
   - Integrates 6DoF head tracking poses from `vxr-input` directly into OpenXR reference spaces (`VIEW`, `LOCAL`, `STAGE`).

5. **Active Runtime Switcher CLI (`vxr_xr_switcher.exe`)**:
   - Utility for managing the active OpenXR runtime in the Windows Registry (`HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`).
   - Supports instant switching to VXR-XR and restoring SteamVR or Oculus runtimes.

---

## Runtime Manifest Configuration

Registering VXR-XR as the active runtime requires setting the Windows Registry string value:
```text
HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime = "E:\Code\ProjectVXR\drivers\openxr\openxr_vxr.json"
```

Or via the bundled switcher CLI:
```powershell
.\drivers\openxr\build\Release\vxr_xr_switcher.exe --set-vxr
```

---

## Build & Test

```powershell
cmake -S drivers/openxr -B drivers/openxr/build -G "Visual Studio 16 2019" -A x64
cmake --build drivers/openxr/build --config Release
.\drivers\openxr\build\Release\test_openxr_runtime.exe
.\drivers\openxr\build\Release\vxr_xr_switcher.exe --status
```
