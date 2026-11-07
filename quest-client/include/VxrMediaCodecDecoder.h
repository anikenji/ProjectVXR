#ifndef VXR_MEDIACODEC_DECODER_H
#define VXR_MEDIACODEC_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace vxr {

enum class VideoCodecType {
    H264 = 1,
    Hevc = 2,
    Av1 = 3,
};

enum class DecoderStatus {
    Success = 0,
    TryAgainLater = 1,
    FormatChanged = 2,
    BufferInfo = 3,
    Error = -1,
    NotInitialized = -2,
};

struct DecodedOutputFrame {
    int32_t buffer_index;
    size_t size;
    uint64_t pts_us;
    uint32_t flags;
    void* hardware_buffer; // Pointer to AHardwareBuffer (for zero-copy Vulkan import)
    uint32_t width;
    uint32_t height;
};

/// AMediaCodec Hardware Video Decoder configured for ultra-low latency VR streaming
class VxrMediaCodecDecoder {
public:
    VxrMediaCodecDecoder();
    ~VxrMediaCodecDecoder();

    bool Initialize(VideoCodecType codec, uint32_t width, uint32_t height, uint32_t fps);
    bool QueueInputBuffer(const uint8_t* nalu_data, size_t size, uint64_t pts_us, bool is_keyframe);
    DecoderStatus DequeueOutputBuffer(DecodedOutputFrame* out_frame, uint64_t timeout_us);
    bool ReleaseOutputBuffer(int32_t buffer_index, bool render);

    void Flush();
    void Cleanup();

    bool IsInitialized() const { return m_initialized; }
    VideoCodecType GetCodec() const { return m_codec; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    uint32_t GetFps() const { return m_fps; }
    bool IsLowLatencyEnabled() const { return m_lowLatencyEnabled; }
    uint64_t GetDecodedFrameCount() const { return m_decodedFrameCount; }

private:
    VideoCodecType m_codec;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    bool m_initialized;
    bool m_lowLatencyEnabled;
    uint64_t m_decodedFrameCount;

    // Simulated / portable codec handle for cross-platform validation
    void* m_mediaCodec;
    void* m_mediaFormat;
};

} // namespace vxr

#endif // VXR_MEDIACODEC_DECODER_H
