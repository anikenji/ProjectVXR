#include "VxrMultiDecoder.h"
#include <cstring>

namespace vxr {

VxrMultiDecoder::VxrMultiDecoder() {
    // Default ergonomic 4-monitor spatial layout around the user
    const float xOffsets[VXR_MAX_MONITOR_STREAMS] = { 0.0f, -1.85f, 1.85f, 0.0f };
    const float yOffsets[VXR_MAX_MONITOR_STREAMS] = { 1.50f, 1.50f, 1.50f, 2.55f };
    const float zOffsets[VXR_MAX_MONITOR_STREAMS] = { -2.0f, -1.75f, -1.75f, -1.90f };

    for (uint32_t i = 0; i < VXR_MAX_MONITOR_STREAMS; ++i) {
        m_monitorPoses[i].position[0] = xOffsets[i];
        m_monitorPoses[i].position[1] = yOffsets[i];
        m_monitorPoses[i].position[2] = zOffsets[i];
        m_monitorPoses[i].orientation[0] = 0.0f;
        m_monitorPoses[i].orientation[1] = 0.0f;
        m_monitorPoses[i].orientation[2] = 0.0f;
        m_monitorPoses[i].orientation[3] = 1.0f;
        m_monitorPoses[i].width_m = 1.80f;
        m_monitorPoses[i].height_m = 1.0125f;
        m_monitorPoses[i].curvature = 0.25f;
        m_monitorPoses[i].active = (i == 0);
    }
}

VxrMultiDecoder::~VxrMultiDecoder() {
    Cleanup();
}

bool VxrMultiDecoder::InitializeStream(uint32_t stream_index,
                                       VideoCodecType codec,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t fps) {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS) {
        return false;
    }
    bool ok = m_decoders[stream_index].Initialize(codec, width, height, fps);
    m_monitorPoses[stream_index].active = ok;
    return ok;
}

bool VxrMultiDecoder::InitializeAllStreams(VideoCodecType codec,
                                           uint32_t width,
                                           uint32_t height,
                                           uint32_t fps) {
    bool allOk = true;
    for (uint32_t i = 0; i < VXR_MAX_MONITOR_STREAMS; ++i) {
        if (!InitializeStream(i, codec, width, height, fps)) {
            allOk = false;
        }
    }
    return allOk;
}

bool VxrMultiDecoder::FeedSlicedPacket(const uint8_t* packet_data, size_t packet_size, bool is_keyframe) {
    if (!packet_data || packet_size <= sizeof(VxrSliceHeader)) {
        return false;
    }

    VxrSliceHeader hdr = {};
    std::memcpy(&hdr, packet_data, sizeof(VxrSliceHeader));

    if (hdr.stream_index >= VXR_MAX_MONITOR_STREAMS || hdr.data_size == 0) {
        return false;
    }
    if (sizeof(VxrSliceHeader) + hdr.data_size > packet_size) {
        return false;
    }

    const uint8_t* payload = packet_data + sizeof(VxrSliceHeader);
    return QueueStreamBuffer(hdr.stream_index, payload, hdr.data_size, hdr.timestamp_us, is_keyframe);
}

bool VxrMultiDecoder::QueueStreamBuffer(uint32_t stream_index,
                                        const uint8_t* nalu_data,
                                        size_t size,
                                        uint64_t pts_us,
                                        bool is_keyframe) {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS || !nalu_data || size == 0) {
        return false;
    }
    if (!m_decoders[stream_index].IsInitialized()) {
        if (!m_decoders[stream_index].Initialize(VideoCodecType::Hevc, 1920, 1080, 90)) {
            return false;
        }
        m_monitorPoses[stream_index].active = true;
    }
    return m_decoders[stream_index].QueueInputBuffer(nalu_data, size, pts_us, is_keyframe);
}

DecoderStatus VxrMultiDecoder::DequeueStreamOutput(uint32_t stream_index,
                                                   DecodedOutputFrame* out_frame,
                                                   uint64_t timeout_us) {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS || !out_frame) {
        return DecoderStatus::Error;
    }
    if (!m_decoders[stream_index].IsInitialized()) {
        return DecoderStatus::NotInitialized;
    }
    return m_decoders[stream_index].DequeueOutputBuffer(out_frame, timeout_us);
}

bool VxrMultiDecoder::ReleaseStreamOutput(uint32_t stream_index, int32_t buffer_index, bool render) {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS) {
        return false;
    }
    return m_decoders[stream_index].ReleaseOutputBuffer(buffer_index, render);
}

void VxrMultiDecoder::FlushAll() {
    for (uint32_t i = 0; i < VXR_MAX_MONITOR_STREAMS; ++i) {
        if (m_decoders[i].IsInitialized()) {
            m_decoders[i].Flush();
        }
    }
}

void VxrMultiDecoder::Cleanup() {
    for (uint32_t i = 0; i < VXR_MAX_MONITOR_STREAMS; ++i) {
        m_decoders[i].Cleanup();
        m_monitorPoses[i].active = false;
    }
}

VxrMediaCodecDecoder* VxrMultiDecoder::GetDecoder(uint32_t stream_index) {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS) {
        return nullptr;
    }
    return &m_decoders[stream_index];
}

const VxrMediaCodecDecoder* VxrMultiDecoder::GetDecoder(uint32_t stream_index) const {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS) {
        return nullptr;
    }
    return &m_decoders[stream_index];
}

bool VxrMultiDecoder::IsStreamInitialized(uint32_t stream_index) const {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS) {
        return false;
    }
    return m_decoders[stream_index].IsInitialized();
}

uint32_t VxrMultiDecoder::GetActiveStreamCount() const {
    uint32_t count = 0;
    for (uint32_t i = 0; i < VXR_MAX_MONITOR_STREAMS; ++i) {
        if (m_decoders[i].IsInitialized()) {
            count++;
        }
    }
    return count;
}

uint64_t VxrMultiDecoder::GetTotalDecodedFrames() const {
    uint64_t total = 0;
    for (uint32_t i = 0; i < VXR_MAX_MONITOR_STREAMS; ++i) {
        total += m_decoders[i].GetDecodedFrameCount();
    }
    return total;
}

void VxrMultiDecoder::SetMonitorPose(uint32_t stream_index, const VxrMonitorPose3D& pose) {
    if (stream_index < VXR_MAX_MONITOR_STREAMS) {
        m_monitorPoses[stream_index] = pose;
    }
}

const VxrMonitorPose3D& VxrMultiDecoder::GetMonitorPose(uint32_t stream_index) const {
    if (stream_index >= VXR_MAX_MONITOR_STREAMS) {
        return m_monitorPoses[0];
    }
    return m_monitorPoses[stream_index];
}

} // namespace vxr
