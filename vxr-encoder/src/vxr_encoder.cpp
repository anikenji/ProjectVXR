#include "vxr_encoder.h"
#include "VxrVideoEncoder.h"

extern "C" {

VxrEncoderHandle vxr_encoder_create(VxrEncoderBackend backend) {
    auto encoder = vxr::CreateVideoEncoder(backend);
    return encoder.release();
}

bool vxr_encoder_init(VxrEncoderHandle handle, const VxrEncoderConfig* config, void* d3d11_device) {
    if (!handle || !config) return false;
    auto enc = static_cast<vxr::IVxrVideoEncoder*>(handle);
    return enc->Initialize(*config, d3d11_device);
}

bool vxr_encoder_encode_texture(
    VxrEncoderHandle handle,
    void* d3d11_texture,
    uint64_t pts_us,
    bool force_idr,
    VxrBitstreamFrame* out_frame
) {
    if (!handle || !out_frame) return false;
    auto enc = static_cast<vxr::IVxrVideoEncoder*>(handle);
    return enc->EncodeTexture(d3d11_texture, pts_us, force_idr, out_frame);
}

bool vxr_encoder_reconfigure_bitrate(VxrEncoderHandle handle, uint32_t bitrate_bps, uint32_t fps) {
    if (!handle) return false;
    auto enc = static_cast<vxr::IVxrVideoEncoder*>(handle);
    return enc->ReconfigureBitrate(bitrate_bps, fps);
}

void vxr_encoder_destroy(VxrEncoderHandle handle) {
    if (handle) {
        auto enc = static_cast<vxr::IVxrVideoEncoder*>(handle);
        delete enc;
    }
}

} // extern "C"
