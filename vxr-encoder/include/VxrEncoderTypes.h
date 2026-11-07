#ifndef VXR_ENCODER_TYPES_H
#define VXR_ENCODER_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VxrEncoderBackend {
    VXR_BACKEND_AUTO = 0,
    VXR_BACKEND_NVENC = 1,
    VXR_BACKEND_AMF = 2,
    VXR_BACKEND_SOFTWARE_SIM = 3,
} VxrEncoderBackend;

typedef enum VxrVideoCodec {
    VXR_CODEC_H264 = 1,
    VXR_CODEC_HEVC = 2,      // H.265 (8-bit or 10-bit P010)
    VXR_CODEC_AV1 = 3,       // AV1 (10-bit, RTX 40-series Ada Lovelace / Quest 3 XR2 Gen 2)
} VxrVideoCodec;

typedef enum VxrColorFormat {
    VXR_COLOR_NV12 = 1,      // 8-bit 4:2:0
    VXR_COLOR_P010 = 2,      // 10-bit 4:2:0 (eliminates color banding)
    VXR_COLOR_RGBA = 3,      // 8-bit RGBA
} VxrColorFormat;

typedef struct VxrEncoderConfig {
    VxrVideoCodec codec;
    VxrColorFormat color_format;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bitrate_bps;           // e.g. 100_000_000 to 500_000_000 (100 - 500 Mbps)
    uint32_t max_bitrate_bps;
    bool enable_two_pass;           // 2-Pass encoding for sharp edges
    bool enable_spatial_aq;         // Spatial Adaptive Quantization
    bool enable_temporal_aq;        // Temporal Adaptive Quantization
    bool enable_intra_refresh;      // Periodic Intra-Refresh (rolls intra blocks over frames)
    uint32_t intra_refresh_period;  // e.g. 60 frames
    uint32_t num_slices;            // Sliced encoding: e.g. 4 to 8 slices per frame
} VxrEncoderConfig;

typedef struct VxrBitstreamFrame {
    uint8_t* data;
    size_t size;
    uint64_t pts_us;
    uint32_t frame_index;
    bool is_keyframe;
    bool is_intra_refresh;
    uint32_t num_slices;
    uint32_t codec_id;
} VxrBitstreamFrame;

#ifdef __cplusplus
}
#endif

#endif // VXR_ENCODER_TYPES_H
