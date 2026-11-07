#ifndef VXR_ENCODER_H
#define VXR_ENCODER_H

#include "VxrEncoderTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef VXR_ENCODER_EXPORTS
    #define VXR_ENC_API __declspec(dllexport)
  #else
    #define VXR_ENC_API
  #endif
#else
  #define VXR_ENC_API
#endif

typedef void* VxrEncoderHandle;

/// Create hardware video encoder instance (NVENC / AMF / Auto)
VXR_ENC_API VxrEncoderHandle vxr_encoder_create(VxrEncoderBackend backend);

/// Initialize encoder with Direct3D 11 device and stream configuration
VXR_ENC_API bool vxr_encoder_init(VxrEncoderHandle handle, const VxrEncoderConfig* config, void* d3d11_device);

/// Encodes a Direct3D 11 texture zero-copy directly on GPU VRAM, returning compressed NALU bitstream
VXR_ENC_API bool vxr_encoder_encode_texture(
    VxrEncoderHandle handle,
    void* d3d11_texture,
    uint64_t pts_us,
    bool force_idr,
    VxrBitstreamFrame* out_frame
);

/// Dynamically updates the bitrate and FPS in-flight from network congestion control feedback
VXR_ENC_API bool vxr_encoder_reconfigure_bitrate(VxrEncoderHandle handle, uint32_t bitrate_bps, uint32_t fps);

/// Destroys encoder instance and releases GPU sessions
VXR_ENC_API void vxr_encoder_destroy(VxrEncoderHandle handle);

#ifdef __cplusplus
}
#endif

#endif // VXR_ENCODER_H
