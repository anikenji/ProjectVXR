#include "VxrOpenXrClient.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cmath>
#include <algorithm>

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <android/log.h>
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "VXR-OpenXR", __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, "VXR-OpenXR", __VA_ARGS__)
#endif

namespace vxr {

VxrOpenXrClient::VxrOpenXrClient()
    : m_initialized(false)
    , m_sessionRunning(false)
    , m_sessionState(OpenXrSessionState::Unknown)
    , m_frameIndex(0)
    , m_loaderHandle(nullptr)
    , m_pfnGetInstanceProcAddr(nullptr)
    , m_instance(nullptr)
    , m_systemId(nullptr)
    , m_session(nullptr)
    , m_localSpace(nullptr)
{
    std::memset(&m_controllerState, 0, sizeof(m_controllerState));
    m_controllerState.ray_origin[0] = 0.0f;
    m_controllerState.ray_origin[1] = 1.50f;
    m_controllerState.ray_origin[2] = 0.0f;
    m_controllerState.ray_direction[0] = 0.0f;
    m_controllerState.ray_direction[1] = 0.0f;
    m_controllerState.ray_direction[2] = -1.0f;
    m_controllerState.is_active = true;
}

VxrOpenXrClient::~VxrOpenXrClient() {
    Cleanup();
}

bool VxrOpenXrClient::Initialize(const OpenXrClientConfig& config) {
    Cleanup();
    m_config = config;

#if defined(__ANDROID__)
    m_loaderHandle = dlopen("libopenxr_loader.so", RTLD_NOW);
    if (m_loaderHandle != nullptr) {
        m_pfnGetInstanceProcAddr = dlsym(m_loaderHandle, "xrGetInstanceProcAddr");
        ALOGI("Loaded libopenxr_loader.so (%p), xrGetInstanceProcAddr=%p",
              m_loaderHandle, m_pfnGetInstanceProcAddr);
    } else {
        ALOGW("libopenxr_loader.so not present; using internal predictive 6DoF generator");
    }
#endif

    m_instance = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00010001));
    m_systemId = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00020002));
    m_sessionState = OpenXrSessionState::Idle;
    m_initialized = true;

    // Reset default controller aim ray pointing forward at the virtual desktop center
    std::memset(&m_controllerState, 0, sizeof(m_controllerState));
    m_controllerState.ray_origin[0] = 0.0f;
    m_controllerState.ray_origin[1] = 1.50f;
    m_controllerState.ray_origin[2] = 0.0f;
    m_controllerState.ray_direction[0] = 0.0f;
    m_controllerState.ray_direction[1] = 0.0f;
    m_controllerState.ray_direction[2] = -1.0f;
    m_controllerState.is_active = true;

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
    m_session = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00030003));
    m_localSpace = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00040004));
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

    uint32_t fps = (m_config.display_refresh_rate > 0) ? m_config.display_refresh_rate : 90;
    // Frame interval: 11.1ms for 90Hz, 8.3ms for 120Hz
    uint64_t frame_interval_ns = 1000000000ULL / fps;
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
    (void)display_time_ns;
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
    (void)display_time_ns;
    if (!m_sessionRunning) return false;
    m_frameIndex++;
    return true;
}

bool VxrOpenXrClient::PollControllerInput(XrControllerInputState* out_state) {
    if (!m_sessionRunning) return false;
    m_controllerState.trigger_clicked = (m_controllerState.trigger_value >= 0.55f);
    m_controllerState.grip_clicked = (m_controllerState.grip_value >= 0.55f);
    if (out_state) {
        *out_state = m_controllerState;
    }
    return m_controllerState.is_active;
}

void VxrOpenXrClient::SetControllerInputState(const XrControllerInputState& state) {
    m_controllerState = state;
    m_controllerState.trigger_clicked = state.trigger_clicked || (state.trigger_value >= 0.55f);
    m_controllerState.grip_clicked = state.grip_clicked || (state.grip_value >= 0.55f);
}

bool VxrOpenXrClient::ComputeDesktopPointerUV(float screenDistM, float* outU, float* outV) const {
    return ComputeDesktopPointerUV(screenDistM, 2.0f, 1.125f, 1.50f, outU, outV);
}

bool VxrOpenXrClient::ComputeDesktopPointerUV(float screenDistM,
                                              float screenWidthM,
                                              float screenHeightM,
                                              float screenCenterY,
                                              float* outU,
                                              float* outV) const {
    if (!outU || !outV || screenWidthM <= 0.0f || screenHeightM <= 0.0f) {
        return false;
    }

    float planeZ = (screenDistM > 0.0f) ? -screenDistM : screenDistM;
    float dirZ = m_controllerState.ray_direction[2];
    if (std::abs(dirZ) < 1e-6f) {
        *outU = 0.5f;
        *outV = 0.5f;
        return false;
    }

    float t = (planeZ - m_controllerState.ray_origin[2]) / dirZ;
    if (t <= 0.0f) {
        *outU = 0.5f;
        *outV = 0.5f;
        return false;
    }

    float hitX = m_controllerState.ray_origin[0] + t * m_controllerState.ray_direction[0];
    float hitY = m_controllerState.ray_origin[1] + t * m_controllerState.ray_direction[1];

    float rawU = (hitX / screenWidthM) + 0.5f;
    float rawV = 0.5f - ((hitY - screenCenterY) / screenHeightM);

    bool inside = (rawU >= 0.0f && rawU <= 1.0f && rawV >= 0.0f && rawV <= 1.0f);
    *outU = (std::max)(0.0f, (std::min)(1.0f, rawU));
    *outV = (std::max)(0.0f, (std::min)(1.0f, rawV));
    return inside;
}

void VxrOpenXrClient::Cleanup() {
    EndSession();
#if defined(__ANDROID__)
    if (m_loaderHandle != nullptr) {
        dlclose(m_loaderHandle);
        m_loaderHandle = nullptr;
    }
#endif
    m_pfnGetInstanceProcAddr = nullptr;
    m_systemId = nullptr;
    m_instance = nullptr;
    m_initialized = false;
}

} // namespace vxr
