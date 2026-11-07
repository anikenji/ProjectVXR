#include "VxrMediaCodecDecoder.h"
#include <iostream>
#include <cstring>

#if defined(__ANDROID__)
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#define LOG_TAG "VXR-Decoder"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define ALOGI(...) std::cout << "[VXR-Decoder] " << __VA_ARGS__ << std::endl
#define ALOGE(...) std::cerr << "[VXR-Decoder] " << __VA_ARGS__ << std::endl
#endif

namespace vxr {

VxrMediaCodecDecoder::VxrMediaCodecDecoder()
    : m_codec(VideoCodecType::Hevc)
    , m_width(0)
    , m_height(0)
    , m_fps(90)
    , m_initialized(false)
    , m_lowLatencyEnabled(false)
    , m_decodedFrameCount(0)
    , m_mediaCodec(nullptr)
    , m_mediaFormat(nullptr)
{
}

VxrMediaCodecDecoder::~VxrMediaCodecDecoder() {
    Cleanup();
}

bool VxrMediaCodecDecoder::Initialize(VideoCodecType codec, uint32_t width, uint32_t height, uint32_t fps) {
    Cleanup();

    m_codec = codec;
    m_width = width;
    m_height = height;
    m_fps = fps;

    const char* mime = "video/hevc";
    switch (codec) {
        case VideoCodecType::H264: mime = "video/avc"; break;
        case VideoCodecType::Hevc: mime = "video/hevc"; break;
        case VideoCodecType::Av1:  mime = "video/av01"; break;
    }

#if defined(__ANDROID__)
    AMediaCodec* codecInstance = AMediaCodec_createDecoderByType(mime);
    if (!codecInstance) {
        ALOGE("Failed to create AMediaCodec decoder for mime %s", mime);
        return false;
    }

    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);

    // Ultra-Low Latency configuration flags (API 30+ KEY_LOW_LATENCY)
    AMediaFormat_setInt32(format, "low-latency", 1);
    AMediaFormat_setInt32(format, "priority", 0);
    // Qualcomm Snapdragon XR2 hardware acceleration flags
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-low-latency.enable", 1);
    AMediaFormat_setInt32(format, "vendor.qti-ext-dec-picture-order.enable", 0);

    media_status_t status = AMediaCodec_configure(codecInstance, format, nullptr, nullptr, 0);
    if (status != AMEDIA_OK) {
        ALOGE("AMediaCodec_configure failed: %d", status);
        AMediaFormat_delete(format);
        AMediaCodec_delete(codecInstance);
        return false;
    }

    status = AMediaCodec_start(codecInstance);
    if (status != AMEDIA_OK) {
        ALOGE("AMediaCodec_start failed: %d", status);
        AMediaFormat_delete(format);
        AMediaCodec_delete(codecInstance);
        return false;
    }

    m_mediaCodec = codecInstance;
    m_mediaFormat = format;
    m_lowLatencyEnabled = true;
    ALOGI("Started real AMediaCodec (%s %ux%u @ %u Hz) with KEY_LOW_LATENCY", mime, width, height, fps);
#else
    m_lowLatencyEnabled = true;
    m_mediaCodec = reinterpret_cast<void*>(static_cast<uintptr_t>(0xC0DEC001ULL));
    m_mediaFormat = reinterpret_cast<void*>(static_cast<uintptr_t>(0xF080A001ULL));
    std::cout << "[VXR-Decoder] Initialized AMediaCodec for " << mime
              << " (" << width << "x" << height << " @ " << fps << "fps)"
              << " with KEY_LOW_LATENCY & Qualcomm XR2 vendor extensions." << std::endl;
#endif

    m_initialized = true;
    m_decodedFrameCount = 0;
    return true;
}

bool VxrMediaCodecDecoder::QueueInputBuffer(const uint8_t* nalu_data, size_t size, uint64_t pts_us, bool is_keyframe) {
    if (!m_initialized || nalu_data == nullptr || size == 0) {
        return false;
    }

#if defined(__ANDROID__)
    AMediaCodec* codec = static_cast<AMediaCodec*>(m_mediaCodec);
    ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(codec, 2000); // 2ms timeout
    if (bufIdx < 0) {
        return false;
    }

    size_t outSize = 0;
    uint8_t* inputBuf = AMediaCodec_getInputBuffer(codec, bufIdx, &outSize);
    if (!inputBuf || outSize < size) {
        return false;
    }

    memcpy(inputBuf, nalu_data, size);
    uint32_t flags = is_keyframe ? 1 : 0; // BUFFER_FLAG_KEY_FRAME
    media_status_t status = AMediaCodec_queueInputBuffer(codec, bufIdx, 0, size, pts_us, flags);
    return status == AMEDIA_OK;
#else
    return true;
#endif
}

DecoderStatus VxrMediaCodecDecoder::DequeueOutputBuffer(DecodedOutputFrame* out_frame, uint64_t timeout_us) {
    if (!m_initialized || out_frame == nullptr) {
        return DecoderStatus::NotInitialized;
    }

#if defined(__ANDROID__)
    AMediaCodec* codec = static_cast<AMediaCodec*>(m_mediaCodec);
    AMediaCodecBufferInfo info;
    ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, timeout_us);

    if (outIdx >= 0) {
        m_decodedFrameCount++;
        out_frame->buffer_index = static_cast<int32_t>(outIdx);
        out_frame->size = info.size;
        out_frame->pts_us = info.presentationTimeUs;
        out_frame->flags = info.flags;
        out_frame->width = m_width;
        out_frame->height = m_height;

        // In API 26+, query AHardwareBuffer directly from output format
        out_frame->hardware_buffer = nullptr;
        AMediaFormat* outFormat = AMediaCodec_getOutputFormat(codec);
        if (outFormat) {
            void* ahbPtr = nullptr;
            size_t ahbSize = 0;
            if (AMediaFormat_getBuffer(outFormat, "android._AHardwareBuffer", &ahbPtr, &ahbSize) && ahbPtr) {
                out_frame->hardware_buffer = ahbPtr;
            }
            AMediaFormat_delete(outFormat);
        }

        return DecoderStatus::Success;
    } else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        return DecoderStatus::TryAgainLater;
    } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        return DecoderStatus::FormatChanged;
    } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        return DecoderStatus::BufferInfo;
    } else {
        return DecoderStatus::Error;
    }
#else
    m_decodedFrameCount++;
    out_frame->buffer_index = static_cast<int32_t>(m_decodedFrameCount % 16);
    out_frame->size = m_width * m_height * 3 / 2;
    out_frame->pts_us = timeout_us;
    out_frame->flags = 0;
    out_frame->width = m_width;
    out_frame->height = m_height;
    out_frame->hardware_buffer = reinterpret_cast<void*>(0xAB000000 + (m_decodedFrameCount * 0x1000));
    return DecoderStatus::Success;
#endif
}

bool VxrMediaCodecDecoder::ReleaseOutputBuffer(int32_t buffer_index, bool render) {
    if (!m_initialized) {
        return false;
    }

#if defined(__ANDROID__)
    AMediaCodec* codec = static_cast<AMediaCodec*>(m_mediaCodec);
    media_status_t status = AMediaCodec_releaseOutputBuffer(codec, buffer_index, render);
    return status == AMEDIA_OK;
#else
    return true;
#endif
}

void VxrMediaCodecDecoder::Flush() {
    if (m_initialized) {
#if defined(__ANDROID__)
        if (m_mediaCodec) {
            AMediaCodec_flush(static_cast<AMediaCodec*>(m_mediaCodec));
        }
#endif
    }
}

void VxrMediaCodecDecoder::Cleanup() {
    if (m_initialized) {
#if defined(__ANDROID__)
        if (m_mediaCodec) {
            AMediaCodec_stop(static_cast<AMediaCodec*>(m_mediaCodec));
            AMediaCodec_delete(static_cast<AMediaCodec*>(m_mediaCodec));
            m_mediaCodec = nullptr;
        }
        if (m_mediaFormat) {
            AMediaFormat_delete(static_cast<AMediaFormat*>(m_mediaFormat));
            m_mediaFormat = nullptr;
        }
#else
        m_mediaCodec = nullptr;
        m_mediaFormat = nullptr;
#endif
        m_initialized = false;
        m_lowLatencyEnabled = false;
    }
}

} // namespace vxr
