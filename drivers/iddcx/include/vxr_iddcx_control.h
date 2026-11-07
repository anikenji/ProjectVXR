#ifndef VXR_IDDCX_CONTROL_H
#define VXR_IDDCX_CONTROL_H

#include "VxrIddTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef VXR_IDDCX_EXPORTS
    #define VXR_IDD_API __declspec(dllexport)
  #else
    #define VXR_IDD_API
  #endif
#else
  #define VXR_IDD_API
#endif

/// User-mode control API to manage virtual headless monitors for Project VXR

/// Spawns a virtual monitor (supports up to 3 concurrent monitors).
/// Returns monitor_id (1, 2, or 3), or 0 on failure.
VXR_IDD_API uint32_t VxrIdd_CreateMonitor(uint32_t width, uint32_t height, uint32_t refresh_rate);

/// Destroys an active virtual monitor
VXR_IDD_API bool VxrIdd_DestroyMonitor(uint32_t monitor_id);

/// Modifies resolution and refresh rate of an active monitor (supports up to 4K @ 120Hz)
VXR_IDD_API bool VxrIdd_SetResolution(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate);

/// Queries count of active virtual monitors
VXR_IDD_API uint32_t VxrIdd_GetMonitorCount(void);

/// Queries descriptor and supported modes for an active monitor
VXR_IDD_API bool VxrIdd_GetMonitorInfo(uint32_t monitor_id, VxrMonitorDesc* out_desc);

/// Acquires the latest rendered desktop swapchain frame for a virtual monitor
VXR_IDD_API bool VxrIdd_AcquireNextFrame(uint32_t monitor_id, uint32_t timeout_ms, VxrIddFrame* out_frame);

/// Releases acquired swapchain frame buffer
VXR_IDD_API bool VxrIdd_ReleaseFrame(uint32_t monitor_id);

/// Helper: Synthesizes a valid 128-byte VESA EDID block
VXR_IDD_API bool VxrIdd_SynthesizeEdid(
    uint32_t monitor_id,
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate,
    uint8_t out_edid[VXR_IDD_EDID_BLOCK_SIZE]
);

/// Helper: Validates EDID magic header and modulo 256 checksum
VXR_IDD_API bool VxrIdd_ValidateEdid(const uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE]);

#ifdef __cplusplus
}
#endif

#endif // VXR_IDDCX_CONTROL_H
