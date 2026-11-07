#ifndef VXR_OPENXR_CLIENT_H
#define VXR_OPENXR_CLIENT_H

#include <stdint.h>
#include <string>
#include <vector>

namespace vxr {

enum class OpenXrSessionState {
    Unknown = 0,
    Idle = 1,
    Ready = 2,
    Synchronized = 3,
    Visible = 4,
    Focused = 5,
    Stopping = 6,
    LossPending = 7,
    Exiting = 8,
};

struct XrEyeView {
    float position[3];
    float orientation[4]; // x, y, z, w
    float fov_left;
    float fov_right;
    float fov_top;
    float fov_bottom;
};

struct OpenXrClientConfig {
    std::string app_name;
    bool enable_space_warp_ssw;
    bool enable_foveated_rendering;
    uint32_t display_refresh_rate; // 72, 90, 120 Hz
};

/// OpenXR Mobile Client managing session lifecycles, 6DoF tracking, and frame timing
class VxrOpenXrClient {
public:
    VxrOpenXrClient();
    ~VxrOpenXrClient();

    bool Initialize(const OpenXrClientConfig& config);
    void Cleanup();

    /// Transitions session states according to OpenXR runtime events
    void HandleSessionStateChanged(OpenXrSessionState new_state);

    /// Begins OpenXR session (called when state enters READY)
    bool BeginSession();

    /// Ends OpenXR session (called when state enters STOPPING)
    bool EndSession();

    /// Waits for the next display frame timing (xrWaitFrame)
    bool WaitFrame(uint64_t* out_predicted_display_time_ns);

    /// Begins the frame rendering cycle (xrBeginFrame)
    bool BeginFrame();

    /// Locates head views for both eyes at predicted display time
    bool LocateViews(uint64_t display_time_ns, XrEyeView out_views[2]);

    /// Submits rendered layers to OpenXR compositor (xrEndFrame)
    bool EndFrame(uint64_t display_time_ns);

    OpenXrSessionState GetSessionState() const { return m_sessionState; }
    bool IsSessionRunning() const { return m_sessionRunning; }
    bool IsFocused() const { return m_sessionState == OpenXrSessionState::Focused; }
    bool IsSpaceWarpEnabled() const { return m_config.enable_space_warp_ssw; }
    uint32_t GetRefreshRate() const { return m_config.display_refresh_rate; }
    uint64_t GetFrameIndex() const { return m_frameIndex; }

private:
    OpenXrClientConfig m_config;
    bool m_initialized;
    bool m_sessionRunning;
    OpenXrSessionState m_sessionState;
    uint64_t m_frameIndex;

    void* m_instance;            // XrInstance
    void* m_systemId;            // XrSystemId
    void* m_session;             // XrSession
    void* m_localSpace;          // XrSpace
};

} // namespace vxr

#endif // VXR_OPENXR_CLIENT_H
