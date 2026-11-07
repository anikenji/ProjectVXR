# Project VXR - Native Drivers (C++)

This directory houses low-level Windows hardware drivers and VR runtime integration modules:

- **iddcx/**: Windows Indirect Display Driver (IDDCX / UMDF v2) for headless virtual display creation with custom resolutions and refresh rates (72Hz, 90Hz, 120Hz).
- **openvr/**: SteamVR driver implementation (`driver_vrserver.dll`) for streaming VR frames directly to SteamVR.
- **openxr/**: Native OpenXR runtime layer (`openxr_vxr.dll`) for direct-to-OpenXR titles without SteamVR overhead (similar to VDXR).
