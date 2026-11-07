#include "VxrOpenXrClient.h"
#include <iostream>
#include <chrono>

namespace vxr {

VxrOpenXrClient::VxrOpenXrClient()
    : m_initialized(false)
    , m_sessionRunning(false)
    , m_sessionState(OpenXrSessionState::Unknown)
    , m_frameIndex(0)
    , m_instance(nullptr)
    , m_systemId(nullptr)
    , m_session(nullptr)
    , m_localSpace(nullptr)
{
}

VxrOpenXrClient::~VxrOpenXrClient() {
    Cleanup();
}

bool VxrOpenXrClient::Initialize(const OpenXrClientConfig& config) {
    Cleanup();
    m_config = config;

    m_instance = reinterpret_cast<void*>(0x00010001);
    m_systemId = reinterpret_cast<void*>(0x00020002);
    m_sessionState = OpenXrSessionState::Idle;
    m_initialized = true;

    std::cout << "[VXR-OpenXR] Initialized OpenXR client for '" << config.app_name
              << "' (Display rate: " << config.display_refresh_rate << "Hz, "
              << "SpaceWarp SSW: " << (config.enable_space_warp_ssw ? "ON" : "OFF") << ")" << std::endl;

    // Simulate transition through lifecycle to Ready
    HandleSessionStateChanged(OpenXrSessionState::Ready);
    return true;
}

void VxrOpenXrClient::HandleSessionStateChanged(OpenXrSessionState new_state) {
    m_sessionState = new_state;
    switch (new_state) {
        case OpenXrSessionState::Ready:
            BeginSession();
            break;
        case OpenXrSessionState::Stopping:
            EndSession();
            break;
        default:
            break;
    }
}

bool VxrOpenXrClient::BeginSession() {
    if (!m_initialized) return false;
    m_session = reinterpret_cast<void*>(0x00030003);
    m_localSpace = reinterpret_cast<void*>(0x00040004);
    m_sessionRunning = true;
    m_sessionState = OpenXrSessionState::Focused;
    return true;
}

bool VxrOpenXrClient::EndSession() {
    if (!m_sessionRunning) return false;
    m_sessionRunning = false;
    m_sessionState = OpenXrSessionState::Idle;
    m_localSpace = nullptr;
    m_session = nullptr;
    return true;
}

bool VxrOpenXrClient::WaitFrame(uint64_t* out_predicted_display_time_ns) {
    if (!m_sessionRunning) return false;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    // Frame interval: 11.1ms for 90Hz, 8.3ms for 120Hz
    uint64_t frame_interval_ns = 1000000000ULL / m_config.display_refresh_rate;
    if (out_predicted_display_time_ns) {
        *out_predicted_display_time_ns = now_ns + frame_interval_ns;
    }

    return true;
}

bool VxrOpenXrClient::BeginFrame() {
    if (!m_sessionRunning) return false;
    return true;
}

bool VxrOpenXrClient::LocateViews(uint64_t display_time_ns, XrEyeView out_views[2]) {
    if (!m_sessionRunning || out_views == nullptr) return false;

    // Standard Quest 3 IPD = 0.063m (left eye = -0.0315m, right eye = +0.0315m)
    // Left eye view
    out_views[0].position[0] = -0.0315f;
    out_views[0].position[1] = 1.70f;
    out_views[0].position[2] = 0.0f;
    out_views[0].orientation[0] = 0.0f;
    out_views[0].orientation[1] = 0.0f;
    out_views[0].orientation[2] = 0.0f;
    out_views[0].orientation[3] = 1.0f;
    out_views[0].fov_left = -1.05f;
    out_views[0].fov_right = 1.05f;
    out_views[0].fov_top = -1.05f;
    out_views[0].fov_bottom = 1.05f;

    // Right eye view
    out_views[1].position[0] = 0.0315f;
    out_views[1].position[1] = 1.70f;
    out_views[1].position[2] = 0.0f;
    out_views[1].orientation[0] = 0.0f;
    out_views[1].orientation[1] = 0.0f;
    out_views[1].orientation[2] = 0.0f;
    out_views[1].orientation[3] = 1.0f;
    out_views[1].fov_left = -1.05f;
    out_views[1].fov_right = 1.05f;
    out_views[1].fov_top = -1.05f;
    out_views[1].fov_bottom = 1.05f;

    return true;
}

bool VxrOpenXrClient::EndFrame(uint64_t display_time_ns) {
    if (!m_sessionRunning) return false;
    m_frameIndex++;
    return true;
}

void VxrOpenXrClient::Cleanup() {
    EndSession();
    m_systemId = nullptr;
    m_instance = nullptr;
    m_initialized = false;
}

} // namespace vxr
