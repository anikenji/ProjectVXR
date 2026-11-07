#ifndef VXR_NVENC_ENCODER_H
#define VXR_NVENC_ENCODER_H

#include "VxrVideoEncoder.h"
#include <windows.h>
#include <vector>
#include <mutex>

namespace vxr {

class VxrNvencEncoder : public IVxrVideoEncoder {
public:
    VxrNvencEncoder();
    virtual ~VxrNvencEncoder() override;

    virtual bool Initialize(const VxrEncoderConfig& config, void* d3d11_device) override;
    virtual bool EncodeTexture(void* d3d11_texture, uint64_t pts_us, bool force_idr, VxrBitstreamFrame* out_frame) override;
    virtual bool ReconfigureBitrate(uint32_t bitrate_bps, uint32_t fps) override;
    virtual void Cleanup() override;

    virtual const char* GetEncoderName() const override { return "NVIDIA NVENC Hardware Encoder"; }
    virtual uint64_t GetEncodedFrameCount() const override { return m_frameCount; }
    virtual bool IsInitialized() const override { return m_initialized; }

    bool IsHardwareActive() const { return m_hardwareActive; }
    bool IsTenBitEnabled() const { return m_config.color_format == VXR_COLOR_P010; }
    bool IsTwoPassEnabled() const { return m_config.enable_two_pass; }
    bool IsAdaptiveQuantizationEnabled() const { return m_config.enable_spatial_aq && m_config.enable_temporal_aq; }
    bool IsIntraRefreshEnabled() const { return m_config.enable_intra_refresh; }

private:
    bool LoadNvencLibrary();
    bool CreateNvencSession(void* d3d11_device);
    void SetupNvencConfig();

    VxrEncoderConfig m_config;
    bool m_initialized;
    bool m_hardwareActive;
    uint64_t m_frameCount;

    HMODULE m_hNvencDll;
    void* m_encoderSession;
    void* m_d3d11Device;

    std::vector<uint8_t> m_bitstreamBuffer;
    mutable std::mutex m_encoderMutex;
};

} // namespace vxr

#endif // VXR_NVENC_ENCODER_H
