#ifndef VXR_CAPTURE_H
#define VXR_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef VXR_CAPTURE_EXPORTS
    #define VXR_CAPTURE_API __declspec(dllexport)
  #else
    #define VXR_CAPTURE_API
  #endif
#else
  #define VXR_CAPTURE_API
#endif

typedef void* VxrCaptureHandle;

typedef enum VxrCaptureStatus {
    VXR_CAPTURE_OK = 0,
    VXR_CAPTURE_TIMEOUT = 1,
    VXR_CAPTURE_ACCESS_LOST = 2,
    VXR_CAPTURE_DEVICE_RESET = 3,
    VXR_CAPTURE_INVALID_ARG = -1,
    VXR_CAPTURE_NOT_INITIALIZED = -2,
    VXR_CAPTURE_NO_OUTPUT = -3,
    VXR_CAPTURE_D3D_ERROR = -4,
    VXR_CAPTURE_UNKNOWN_ERROR = -99
} VxrCaptureStatus;

typedef struct VxrCaptureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate_num;
    uint32_t refresh_rate_den;
    int32_t rotation;
    uint32_t adapter_index;
    uint32_t output_index;
    char adapter_name[128];
} VxrCaptureDesc;

typedef struct VxrCursorInfo {
    int32_t visible;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t shape_type; // 1 = Color, 2 = Monochrome, 4 = MaskedColor
    uint32_t shape_buffer_size;
} VxrCursorInfo;

typedef struct VxrCapturedFrame {
    void* d3d11_texture;       // ID3D11Texture2D*
    void* shared_handle;       // Windows HANDLE for cross-API zero-copy sharing
    uint64_t present_time_qpc; // QueryPerformanceCounter timestamp
    uint32_t accumulated_frames;
    VxrCursorInfo cursor;
    int32_t has_new_frame;
} VxrCapturedFrame;

/// Create capture instance
VXR_CAPTURE_API VxrCaptureHandle vxr_capture_create(void);

/// Initialize capture on specified adapter and output display
VXR_CAPTURE_API VxrCaptureStatus vxr_capture_init(VxrCaptureHandle handle, uint32_t adapter_index, uint32_t output_index);

/// Retrieve output display characteristics
VXR_CAPTURE_API VxrCaptureStatus vxr_capture_get_desc(VxrCaptureHandle handle, VxrCaptureDesc* out_desc);

/// Acquire next frame from DXGI Desktop Duplication
VXR_CAPTURE_API VxrCaptureStatus vxr_capture_acquire_frame(VxrCaptureHandle handle, uint32_t timeout_ms, VxrCapturedFrame* out_frame);

/// Release acquired frame buffer lock
VXR_CAPTURE_API VxrCaptureStatus vxr_capture_release_frame(VxrCaptureHandle handle);

/// Reinitialize capture (after access lost / resolution change)
VXR_CAPTURE_API VxrCaptureStatus vxr_capture_reinit(VxrCaptureHandle handle);

/// Destroy capture instance
VXR_CAPTURE_API void vxr_capture_destroy(VxrCaptureHandle handle);

#ifdef __cplusplus
}
#endif

#endif // VXR_CAPTURE_H
