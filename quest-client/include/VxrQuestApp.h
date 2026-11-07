#ifndef VXR_QUEST_APP_H
#define VXR_QUEST_APP_H

#include "VxrMediaCodecDecoder.h"
#include "VxrVulkanRenderer.h"
#include "VxrOpenXrClient.h"
#include "VxrHardwareCursor.h"
#include <memory>

namespace vxr {

struct QuestAppConfig {
    VideoCodecType preferred_codec;
    uint32_t stream_width;
    uint32_t stream_height;
    uint32_t target_fps;
    bool enable_zero_copy;
    bool enable_space_warp;
    bool enable_foveation;
};

/// Main Meta Quest Client Application Coordinator
class VxrQuestApp {
public:
    VxrQuestApp();
    ~VxrQuestApp();

    bool Initialize(const QuestAppConfig& config);
    void Shutdown();

    /// Executes one iteration of the client VR render loop
    bool RunFrame();

    /// Feeds incoming network video packet into the hardware decoder
    bool FeedVideoData(const uint8_t* data, size_t size, uint64_t pts_us, bool is_keyframe);

    /// Updates 0ms hardware cursor position from host input packet
    void UpdateHardwareCursor(float norm_x, float norm_y, bool visible);

    VxrMediaCodecDecoder& GetDecoder() { return m_decoder; }
    VxrVulkanRenderer& GetRenderer() { return m_renderer; }
    VxrOpenXrClient& GetOpenXr() { return m_openXr; }
    VxrHardwareCursor& GetCursor() { return m_cursor; }

    bool IsRunning() const { return m_running; }
    uint64_t GetTotalFramesRendered() const { return m_totalFramesRendered; }

private:
    QuestAppConfig m_config;
    bool m_running;
    uint64_t m_totalFramesRendered;

    VxrMediaCodecDecoder m_decoder;
    VxrVulkanRenderer m_renderer;
    VxrOpenXrClient m_openXr;
    VxrHardwareCursor m_cursor;
};

} // namespace vxr

#endif // VXR_QUEST_APP_H
