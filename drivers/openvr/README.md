# Project VXR: SteamVR OpenVR Driver (driver_vrserver.dll)

This module implements the SteamVR driver subsystem for Project VXR, enabling full VR games and applications running on SteamVR to stream directly to standalone VR headsets (Meta Quest 2, Quest 3, Quest Pro).

---

## Architectural Features

1. **IServerTrackedDeviceProvider**:
   - Entry point loaded by `vrserver.exe` via exported `HmdDriverFactory`.
   - Initializes server driver context and manages virtual VR hardware lifecycles.

2. **Virtual HMD Device (`VxrHmdDevice`)**:
   - Implements `vr::ITrackedDeviceServerDriver`, `vr::IVRDisplayComponent`, and `vr::IVRDriverDirectModeComponent`.
   - **Resolution**: 4128x2208 total (dual 2064x2208 panels matching Meta Quest 3 native display specs).
   - **Refresh Rate**: 90Hz / 120Hz display frequency.
   - **Optics & Projection**: Symmetrical tangent FOV ~96-100 degrees with identity distortion coordinates (Quest GPU handles client-side lens distortion and Qualcomm SGSR / FidelityFX CAS sharpening).
   - **Direct Mode Pipeline**: Intercepts compositor swapchain submissions directly via `IVRDriverDirectModeComponent::SubmitLayer` without window manager overhead.

3. **Dual 6DoF Controllers (`VxrControllerDevice`)**:
   - Implements Left (`TrackedControllerRole_LeftHand`, serial `VXR-CTRL-LEFT`) and Right (`TrackedControllerRole_RightHand`, serial `VXR-CTRL-RIGHT`) controllers.
   - Full 6DoF tracking: position, orientation quaternion, linear and angular velocities.
   - Standard VR input bindings: trigger pull/click, grip pull/click, thumbstick X/Y/click, face buttons (A/B and X/Y), menu button, and haptic feedback component.

4. **External FFI API (`VxrDriverShared.h`)**:
   - `VxrDriver_UpdateHmdPose`: Thread-safe 6DoF head pose injection.
   - `VxrDriver_UpdateControllerPose`: Thread-safe 6DoF controller pose injection.
   - `VxrDriver_UpdateControllerInput`: Thread-safe controller button & analog stick updates.
   - `VxrDriver_IsActive`: Status query.

---

## SteamVR Driver Directory Layout

```
drivers/openvr/
├── driver.vrdrivermanifest            # SteamVR driver registration manifest
├── bin/
│   └── win64/
│       └── driver_vrserver.dll        # Compiled 64-bit OpenVR driver DLL
├── resources/
│   └── input/
│       └── vxr_controller_profile.json# SteamVR input binding profile
├── include/
│   ├── openvr_driver.h
│   ├── VxrDriverShared.h
│   ├── VxrDriverServerProvider.h
│   ├── VxrHmdDevice.h
│   └── VxrControllerDevice.h
├── src/
├── tests/
└── CMakeLists.txt
```

---

## Build & Test

```powershell
cmake -S drivers/openvr -B drivers/openvr/build -G "Visual Studio 16 2019" -A x64
cmake --build drivers/openvr/build --config Release
.\drivers\openvr\build\Release\test_openvr_driver.exe
```
