#ifndef VXR_VIDEO_ENCODER_H
#define VXR_VIDEO_ENCODER_H

#include "VxrEncoderTypes.h"
#include <memory>

namespace vxr {

class IVxrVideoEncoder {
public:
    virtual ~IVxrVideoEncoder() = default;
    virtual bool Initialize(const VxrEncoderConfig& config, void* d3d11_device) = 0;
    virtual bool EncodeTexture(void* d3d11_texture, uint64_t pts_us, bool force_idr, VxrBitstreamFrame* out_frame) = 0;
    virtual bool ReconfigureBitrate(uint32_t bitrate_bps, uint32_t fps) = 0;
    virtual void Cleanup() = 0;
    virtual const char* GetEncoderName() const = 0;
    virtual uint64_t GetEncodedFrameCount() const = 0;
    virtual bool IsInitialized() const = 0;
};

/// Factory to instantiate the best hardware video encoder
std::unique_ptr<IVxrVideoEncoder> CreateVideoEncoder(VxrEncoderBackend backend);

} // namespace vxr

#endif // VXR_VIDEO_ENCODER_H
