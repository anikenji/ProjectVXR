#ifndef VXR_IDD_TYPES_H
#define VXR_IDD_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define VXR_IDD_MAX_MONITORS 3
#define VXR_IDD_EDID_BLOCK_SIZE 128

typedef struct VxrDisplayMode {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate_num;
    uint32_t refresh_rate_den;
} VxrDisplayMode;

typedef struct VxrMonitorDesc {
    uint32_t monitor_id;          // 1, 2, or 3
    char name[64];
    uint32_t current_width;
    uint32_t current_height;
    uint32_t current_refresh_rate;
    bool is_connected;
    uint32_t supported_modes_count;
    VxrDisplayMode supported_modes[16];
} VxrMonitorDesc;

typedef struct VxrIddFrame {
    void* d3d11_texture;          // ID3D11Texture2D*
    void* shared_handle;          // Windows shared NT handle for zero-copy capture
    uint64_t qpc_timestamp;       // QueryPerformanceCounter timestamp
    uint64_t frame_index;
    uint32_t width;
    uint32_t height;
    bool has_new_frame;
} VxrIddFrame;

#endif // VXR_IDD_TYPES_H
