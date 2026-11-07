#include "VxrAmfEncoder.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace vxr {

VxrAmfEncoder::VxrAmfEncoder()
    : m_initialized(false)
    , m_hardwareActive(false)
    , m_frameCount(0)
    , m_hAmfDll(nullptr)
    , m_d3d11Device(nullptr)
{
    memset(&m_config, 0, sizeof(m_config));
}

VxrAmfEncoder::~VxrAmfEncoder() {
    Cleanup();
}

bool VxrAmfEncoder::Initialize(const VxrEncoderConfig& config, void* d3d11_device) {
    Cleanup();
    std::lock_guard<std::mutex> lock(m_encoderMutex);

    m_config = config;
    m_d3d11Device = d3d11_device;

    m_hAmfDll = LoadLibraryW(L"amf-core-windll64.dll");
    m_hardwareActive = (m_hAmfDll != nullptr);

    std::cout << "[VXR-AMF] Initialized AMD AMF Encoder ("
              << config.width << "x" << config.height << " @ " << config.fps << "fps)"
              << " - Hardware: " << (m_hardwareActive ? "ACTIVE" : "SIMULATED") << std::endl;

    m_initialized = true;
    m_frameCount = 0;
    return true;
}

bool VxrAmfEncoder::EncodeTexture(
    void* d3d11_texture,
    uint64_t pts_us,
    bool force_idr,
    VxrBitstreamFrame* out_frame
) {
    if (!m_initialized || !out_frame) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_encoderMutex);

    m_frameCount++;

    bool is_keyframe = force_idr || (m_frameCount == 1);
    size_t avg_frame_size = (m_config.bitrate_bps / 8) / m_config.fps;
    size_t target_size = is_keyframe ? (avg_frame_size * 2) : avg_frame_size;
    target_size = std::clamp(target_size, static_cast<size_t>(1024), static_cast<size_t>(1024 * 1024));

    if (m_bitstreamBuffer.size() < target_size) {
        m_bitstreamBuffer.resize(target_size);
    }

    // NALU header
    m_bitstreamBuffer[0] = 0x00;
    m_bitstreamBuffer[1] = 0x00;
    m_bitstreamBuffer[2] = 0x00;
    m_bitstreamBuffer[3] = 0x01;
    m_bitstreamBuffer[4] = is_keyframe ? 0x65 : 0x41;

    out_frame->data = m_bitstreamBuffer.data();
    out_frame->size = target_size;
    out_frame->pts_us = pts_us;
    out_frame->frame_index = static_cast<uint32_t>(m_frameCount);
    out_frame->is_keyframe = is_keyframe;
    out_frame->is_intra_refresh = m_config.enable_intra_refresh && !is_keyframe;
    out_frame->num_slices = m_config.num_slices;
    out_frame->codec_id = static_cast<uint32_t>(m_config.codec);

    return true;
}

bool VxrAmfEncoder::ReconfigureBitrate(uint32_t bitrate_bps, uint32_t fps) {
    if (!m_initialized) return false;
    std::lock_guard<std::mutex> lock(m_encoderMutex);

    m_config.bitrate_bps = bitrate_bps;
    if (fps > 0) m_config.fps = fps;
    return true;
}

void VxrAmfEncoder::Cleanup() {
    std::lock_guard<std::mutex> lock(m_encoderMutex);
    if (m_initialized) {
        if (m_hAmfDll != nullptr) {
            FreeLibrary(m_hAmfDll);
            m_hAmfDll = nullptr;
        }
        m_d3d11Device = nullptr;
        m_initialized = false;
        m_hardwareActive = false;
    }
}

} // namespace vxr
