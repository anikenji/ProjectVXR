#include "VxrQuestApp.h"
#include <iostream>

namespace vxr {

VxrQuestApp::VxrQuestApp()
    : m_running(false)
    , m_totalFramesRendered(0)
{
    memset(&m_config, 0, sizeof(m_config));
}

VxrQuestApp::~VxrQuestApp() {
    Shutdown();
}

bool VxrQuestApp::Initialize(const QuestAppConfig& config) {
    Shutdown();
    m_config = config;

    // 1. Initialize AMediaCodec hardware video decoder
    if (!m_decoder.Initialize(config.preferred_codec, config.stream_width, config.stream_height, config.target_fps)) {
        std::cerr << "[VXR-App] Failed to initialize AMediaCodec decoder!" << std::endl;
        return false;
    }

    // 2. Initialize Vulkan 1.3 renderer with AHardwareBuffer zero-copy import
    VulkanRendererConfig vkConfig;
    vkConfig.width = config.stream_width;
    vkConfig.height = config.stream_height;
    vkConfig.enable_validation_layers = false;
    vkConfig.enable_zero_copy_ahb = config.enable_zero_copy;
    vkConfig.msaa_samples = 4;

    if (!m_renderer.Initialize(vkConfig)) {
        std::cerr << "[VXR-App] Failed to initialize Vulkan renderer!" << std::endl;
        return false;
    }

    // 3. Initialize OpenXR Mobile client
    OpenXrClientConfig xrConfig;
    xrConfig.app_name = "Project VXR Quest Client";
    xrConfig.enable_space_warp_ssw = config.enable_space_warp;
    xrConfig.enable_foveated_rendering = config.enable_foveation;
    xrConfig.display_refresh_rate = config.target_fps;

    if (!m_openXr.Initialize(xrConfig)) {
        std::cerr << "[VXR-App] Failed to initialize OpenXR client!" << std::endl;
        return false;
    }

    m_running = true;
    m_totalFramesRendered = 0;

    std::cout << "[VXR-App] Meta Quest Client Pipeline initialized successfully: "
              << config.stream_width << "x" << config.stream_height << " @ " << config.target_fps << "Hz"
              << ", Zero-Copy: " << (config.enable_zero_copy ? "ENABLED" : "DISABLED")
              << ", SpaceWarp: " << (config.enable_space_warp ? "ENABLED" : "DISABLED") << std::endl;

    return true;
}

bool VxrQuestApp::FeedVideoData(const uint8_t* data, size_t size, uint64_t pts_us, bool is_keyframe) {
    if (!m_running) return false;
    return m_decoder.QueueInputBuffer(data, size, pts_us, is_keyframe);
}

void VxrQuestApp::UpdateHardwareCursor(float norm_x, float norm_y, bool visible) {
    m_cursor.UpdatePosition(norm_x, norm_y, visible);
}

bool VxrQuestApp::RunFrame() {
    if (!m_running || !m_openXr.IsSessionRunning()) {
        return false;
    }

    // 1. Wait for OpenXR frame timing
    uint64_t predictedDisplayTimeNs = 0;
    if (!m_openXr.WaitFrame(&predictedDisplayTimeNs)) {
        return false;
    }

    // 2. Begin OpenXR frame
    if (!m_openXr.BeginFrame()) {
        return false;
    }

    // 3. Locate head eye poses
    XrEyeView eyeViews[2];
    m_openXr.LocateViews(predictedDisplayTimeNs, eyeViews);

    // 4. Dequeue decoded frame from AMediaCodec
    DecodedOutputFrame outputFrame;
    DecoderStatus decStatus = m_decoder.DequeueOutputBuffer(&outputFrame, 0);

    if (decStatus == DecoderStatus::Success) {
        // 5. Zero-Copy Pipeline: Import AHardwareBuffer directly into Vulkan VkImage
        VulkanTexture streamTexture;
        if (m_renderer.ImportHardwareBuffer(outputFrame.hardware_buffer, outputFrame.width, outputFrame.height, &streamTexture)) {
            // 6. Begin Vulkan render pass
            m_renderer.BeginFrame();

            // 7. Render stream quad for left and right eyes
            m_renderer.RenderStreamQuad(streamTexture, 0);
            m_renderer.RenderStreamQuad(streamTexture, 1);

            // 8. Render 0ms Hardware Cursor plane overlay
            m_renderer.RenderCursorQuad(
                m_cursor.GetNormalizedX(),
                m_cursor.GetNormalizedY(),
                0.035f,
                0.035f,
                m_cursor.IsVisible()
            );

            // 9. End Vulkan render pass
            m_renderer.EndFrame();

            // 10. Clean up imported texture and release buffer back to AMediaCodec
            m_renderer.ReleaseTexture(&streamTexture);
            m_decoder.ReleaseOutputBuffer(outputFrame.buffer_index, false);
        }
    }

    // 11. End OpenXR frame and submit layers to compositor
    m_openXr.EndFrame(predictedDisplayTimeNs);
    m_totalFramesRendered++;

    return true;
}

void VxrQuestApp::Shutdown() {
    if (m_running) {
        m_openXr.Cleanup();
        m_renderer.Cleanup();
        m_decoder.Cleanup();
        m_running = false;
    }
}

} // namespace vxr
