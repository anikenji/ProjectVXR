#ifndef VXR_MULTI_DECODER_H
#define VXR_MULTI_DECODER_H

#include "VxrMediaCodecDecoder.h"
#include <stdint.h>
#include <stddef.h>

namespace vxr {

static constexpr uint32_t VXR_MAX_MONITOR_STREAMS = 4;

#pragma pack(push, 1)
/// 16-byte Multi-Stream Slice Header (Phase C: 4x Parallel AMediaCodec Multi-Monitor Pipeline)
struct VxrSliceHeader {
    uint64_t timestamp_us;  // 8 bytes: Microsecond V-Sync presentation timestamp
    uint32_t stream_index;  // 4 bytes: Target monitor stream index (0..3)
    uint32_t data_size;     // 4 bytes: Encoded NALU payload byte length
};
#pragma pack(pop)

static_assert(sizeof(VxrSliceHeader) == 16, "VxrSliceHeader must be exactly 16 bytes");

/// 3D Spatial Placement for each independent virtual monitor in VR space
struct VxrMonitorPose3D {
    float position[3];      // X, Y, Z in meters
    float orientation[4];   // Quaternion (x, y, z, w)
    float width_m;
    float height_m;
    float curvature;        // 0.0 (flat) .. 1.0 (curved cylinder)
    bool  active;
};

/// Manages 4 parallel hardware AMediaCodec instances on Qualcomm Snapdragon XR2 / XR2 Gen 2
/// for independent multi-monitor streaming without ultrawide texture distortion.
class VxrMultiDecoder {
public:
    VxrMultiDecoder();
    ~VxrMultiDecoder();

    /// Initializes a specific monitor stream decoder (0..3)
    bool InitializeStream(uint32_t stream_index,
                          VideoCodecType codec,
                          uint32_t width,
                          uint32_t height,
                          uint32_t fps);

    /// Initializes all 4 hardware decoders in parallel
    bool InitializeAllStreams(VideoCodecType codec,
                              uint32_t width,
                              uint32_t height,
                              uint32_t fps);

    /// Parses a 16-byte VxrSliceHeader prefixed packet and routes the bitstream payload
    /// to the corresponding hardware AMediaCodec instance (0..3).
    bool FeedSlicedPacket(const uint8_t* packet_data, size_t packet_size, bool is_keyframe);

    /// Directly queues an encoded bitstream buffer into the specified stream's decoder
    bool QueueStreamBuffer(uint32_t stream_index,
                           const uint8_t* nalu_data,
                           size_t size,
                           uint64_t pts_us,
                           bool is_keyframe);

    /// Dequeues a decoded AHardwareBuffer frame from the specified monitor stream (0..3)
    DecoderStatus DequeueStreamOutput(uint32_t stream_index,
                                      DecodedOutputFrame* out_frame,
                                      uint64_t timeout_us = 0);

    /// Releases a dequeued output buffer back to the specified monitor stream's AMediaCodec
    bool ReleaseStreamOutput(uint32_t stream_index, int32_t buffer_index, bool render = false);

    void FlushAll();
    void Cleanup();

    VxrMediaCodecDecoder* GetDecoder(uint32_t stream_index);
    const VxrMediaCodecDecoder* GetDecoder(uint32_t stream_index) const;

    bool IsStreamInitialized(uint32_t stream_index) const;
    uint32_t GetActiveStreamCount() const;
    uint64_t GetTotalDecodedFrames() const;

    void SetMonitorPose(uint32_t stream_index, const VxrMonitorPose3D& pose);
    const VxrMonitorPose3D& GetMonitorPose(uint32_t stream_index) const;

private:
    VxrMediaCodecDecoder m_decoders[VXR_MAX_MONITOR_STREAMS];
    VxrMonitorPose3D m_monitorPoses[VXR_MAX_MONITOR_STREAMS];
};

} // namespace vxr

#endif // VXR_MULTI_DECODER_H
