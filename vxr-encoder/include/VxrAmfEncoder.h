#ifndef VXR_AMF_ENCODER_H
#define VXR_AMF_ENCODER_H

#include "VxrVideoEncoder.h"
#include <windows.h>
#include <vector>
#include <mutex>

namespace vxr {

class VxrAmfEncoder : public IVxrVideoEncoder {
public:
    VxrAmfEncoder();
    virtual ~VxrAmfEncoder() override;

    virtual bool Initialize(const VxrEncoderConfig& config, void* d3d11_device) override;
    virtual bool EncodeTexture(void* d3d11_texture, uint64_t pts_us, bool force_idr, VxrBitstreamFrame* out_frame) override;
    virtual bool ReconfigureBitrate(uint32_t bitrate_bps, uint32_t fps) override;
    virtual void Cleanup() override;

    virtual const char* GetEncoderName() const override { return "AMD AMF Hardware Encoder"; }
    virtual uint64_t GetEncodedFrameCount() const override { return m_frameCount; }
    virtual bool IsInitialized() const override { return m_initialized; }

    bool IsHardwareActive() const { return m_hardwareActive; }

private:
    VxrEncoderConfig m_config;
    bool m_initialized;
    bool m_hardwareActive;
    uint64_t m_frameCount;

    HMODULE m_hAmfDll;
    void* m_d3d11Device;
    std::vector<uint8_t> m_bitstreamBuffer;
    mutable std::mutex m_encoderMutex;
};

} // namespace vxr

#endif // VXR_AMF_ENCODER_H
