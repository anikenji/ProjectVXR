#ifndef VXR_QUEST_APP_H
#define VXR_QUEST_APP_H

#include "VxrMediaCodecDecoder.h"
#include "VxrVulkanRenderer.h"
#include "VxrOpenXrClient.h"
#include "VxrHardwareCursor.h"
#include "VxrVrMenuUi.h"
#include "VxrClientNetwork.h"
#include <memory>
#include <vector>

namespace vxr {

struct QuestAppConfig {
    VideoCodecType preferred_codec = VideoCodecType::Hevc;
    uint32_t stream_width = 2064;
    uint32_t stream_height = 2208;
    uint32_t target_fps = 90;
    bool enable_zero_copy = true;
    bool enable_space_warp = true;
    bool enable_foveation = true;
};

/// Main Meta Quest Client Application Coordinator
/// Integrates OpenXR 6DoF, Vulkan 1.3 Zero-Copy Compositor, AMediaCodec Low-Latency Decoder,
/// Virtual Desktop 7-Tab In-VR Menu Overlay, and Bidirectional UDP Network Engine.
class VxrQuestApp {
public:
    VxrQuestApp();
    ~VxrQuestApp();

    bool Initialize(const QuestAppConfig& config);
    void Shutdown();

    /// Executes one complete iteration of the client VR network, input, UI, decode, and render loop
    bool RunFrame();

    /// Feeds incoming network video packet into the hardware decoder
    bool FeedVideoData(const uint8_t* data, size_t size, uint64_t pts_us, bool is_keyframe);

    /// Updates 0ms hardware cursor position from host input packet
    void UpdateHardwareCursor(float norm_x, float norm_y, bool visible);

    /// Handles direct pointer/laser/touch interaction in normalized [0..1] window coordinates
    void OnSurfacePointerEvent(float norm_x, float norm_y, bool is_down, bool is_click);

    /// Returns the 1280x800 RGBA8888 composited UI + Live Desktop buffer
    const uint32_t* GetCompositedRgbaFrame(uint32_t* out_width, uint32_t* out_height);

    VxrMediaCodecDecoder& GetDecoder() { return m_decoder; }
    VxrVulkanRenderer& GetRenderer() { return m_renderer; }
    VxrOpenXrClient& GetOpenXr() { return m_openXr; }
    VxrHardwareCursor& GetCursor() { return m_cursor; }
    VxrVrMenuUi& GetVrMenu() { return m_vrMenu; }
    VxrClientNetwork& GetNetwork() { return m_network; }

    bool IsRunning() const { return m_running; }
    uint64_t GetTotalFramesRendered() const { return m_totalFramesRendered; }

private:
    void DecodeLiveDesktopStrip(const uint8_t* data, size_t size);
    void CompositeFinalRgbaFrame();

    QuestAppConfig m_config;
    bool m_running;
    uint64_t m_totalFramesRendered;
    bool m_prevMenuBtnPressed;
    bool m_prevTriggerClicked;
    float m_surfacePointerX;
    float m_surfacePointerY;
    bool m_hasSurfacePointer;
    bool m_hasLiveDesktopFrame;

    VxrMediaCodecDecoder m_decoder;
    VxrVulkanRenderer m_renderer;
    VxrOpenXrClient m_openXr;
    VxrHardwareCursor m_cursor;
    VxrVrMenuUi m_vrMenu;
    VxrClientNetwork m_network;

    std::vector<uint32_t> m_menuRgbaBuffer;
    std::vector<uint32_t> m_desktopRgbaBuffer;
    std::vector<uint32_t> m_compositedRgbaBuffer;
    std::vector<uint32_t> m_perfHudRgbaBuffer;
};

} // namespace vxr

#endif // VXR_QUEST_APP_H
