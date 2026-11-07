#include "VxrDriverServerProvider.h"
#include <iostream>
#include <cstring>

namespace vxr {

static const char* const s_InterfaceVersions[] = {
    vr::IVRSettings_Version,
    vr::ITrackedDeviceServerDriver_Version,
    vr::IVRDisplayComponent_Version,
    vr::IVRDriverDirectModeComponent_Version,
    vr::IVRCameraComponent_Version,
    vr::IServerTrackedDeviceProvider_Version,
    vr::IVRWatchdogProvider_Version,
    vr::IVRVirtualDisplay_Version,
    vr::IVRDriverManager_Version,
    vr::IVRResources_Version,
    vr::IVRCompositorPluginProvider_Version,
    "IVRIPCResourceManagerClient_004",
    vr::IVRIPCResourceManagerClient_Version,
    "IVRIPCResourceManagerClient_002",
    nullptr
};

VxrDriverServerProvider& VxrDriverServerProvider::GetInstance() {
    static VxrDriverServerProvider s_instance;
    return s_instance;
}

VxrDriverServerProvider::VxrDriverServerProvider()
    : m_initialized(false)
{
}

VxrDriverServerProvider::~VxrDriverServerProvider() {
    Cleanup();
}

void VxrDriverServerProvider::OpenSharedMemory() {
#ifdef _WIN32
    if (m_pSharedState) return;
    m_hMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(VxrSteamVrSharedState),
        VXR_STEAMVR_SHM_NAME
    );
    if (m_hMapFile) {
        m_pSharedState = reinterpret_cast<VxrSteamVrSharedState*>(
            MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VxrSteamVrSharedState))
        );
    }
#endif
}

void VxrDriverServerProvider::CloseSharedMemory() {
#ifdef _WIN32
    if (m_pSharedState) {
        UnmapViewOfFile(m_pSharedState);
        m_pSharedState = nullptr;
    }
    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
        m_hMapFile = nullptr;
    }
#endif
}

vr::EVRInitError VxrDriverServerProvider::Init(vr::IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    OpenSharedMemory();

    std::string serial = "VXR-HMD-001";
    std::string model  = "Meta Quest 2";

#ifdef _WIN32
    // When loaded inside real SteamVR (pDriverContext != nullptr), only activate VXR HMD
    // if ProjectVXR.exe is running AND a Quest headset is actively connected to ProjectVXR!
    // Otherwise return VRInitError_Init_HmdNotFound (108) so Virtual Desktop / other drivers work normally.
    if (pDriverContext != nullptr) {
        if (!m_pSharedState ||
            m_pSharedState->magic != VXR_STEAMVR_SHM_MAGIC ||
            m_pSharedState->headset_connected == 0) {
            if (vr::VRDriverLog()) {
                vr::VRDriverLog()->Log("[VXR-OpenVR] ProjectVXR Streamer has no connected headset. Returning HmdNotFound(108) so VirtualDesktop/other drivers can take over.\n");
            }
            CloseSharedMemory();
            return vr::VRInitError_Init_HmdNotFound;
        }
    }
    if (m_pSharedState && m_pSharedState->magic == VXR_STEAMVR_SHM_MAGIC) {
        if (m_pSharedState->headset_serial[0] != '\0') {
            serial = m_pSharedState->headset_serial;
        }
        if (m_pSharedState->headset_model[0] != '\0') {
            model = m_pSharedState->headset_model;
        }
    }
#endif

    // Create virtual HMD and dual 6DoF controllers
    m_pHmd = std::make_unique<VxrHmdDevice>(serial, model);
    m_pLeftController = std::make_unique<VxrControllerDevice>(0);  // Left Hand
    m_pRightController = std::make_unique<VxrControllerDevice>(1); // Right Hand

    // Register devices with SteamVR vrserver host
    if (vr::VRServerDriverHost() != nullptr) {
        bool hmdAdded = vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pHmd->GetSerialNumber().c_str(),
            vr::TrackedDeviceClass_HMD,
            m_pHmd.get()
        );
        if (!hmdAdded && vr::VRDriverLog()) {
            vr::VRDriverLog()->Log("[VXR-OpenVR] Failed to add HMD to vrserver host!\n");
        } else if (vr::VRDriverLog()) {
            vr::VRDriverLog()->Log(("[VXR-OpenVR] Activated HMD: " + model + " (" + serial + ")\n").c_str());
        }

        bool leftAdded = vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pLeftController->GetSerialNumber().c_str(),
            vr::TrackedDeviceClass_Controller,
            m_pLeftController.get()
        );
        if (!leftAdded && vr::VRDriverLog()) {
            vr::VRDriverLog()->Log("[VXR-OpenVR] Failed to add Left Controller to vrserver host!\n");
        }

        bool rightAdded = vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pRightController->GetSerialNumber().c_str(),
            vr::TrackedDeviceClass_Controller,
            m_pRightController.get()
        );
        if (!rightAdded && vr::VRDriverLog()) {
            vr::VRDriverLog()->Log("[VXR-OpenVR] Failed to add Right Controller to vrserver host!\n");
        }
    }

    m_initialized.store(true);
    return vr::VRInitError_None;
}

void VxrDriverServerProvider::Cleanup() {
    if (m_initialized.load()) {
        m_pHmd.reset();
        m_pLeftController.reset();
        m_pRightController.reset();
        CloseSharedMemory();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
        m_initialized.store(false);
    }
}

const char* const* VxrDriverServerProvider::GetInterfaceVersions() {
    return s_InterfaceVersions;
}

void VxrDriverServerProvider::RunFrame() {
    if (!m_initialized.load()) return;

#ifdef _WIN32
    if (!m_pSharedState) {
        OpenSharedMemory();
    }
    if (m_pSharedState && m_pSharedState->magic == VXR_STEAMVR_SHM_MAGIC) {
        if (m_pHmd) {
            if (m_pSharedState->headset_model[0] != '\0') {
                m_pHmd->UpdateModelName(m_pSharedState->headset_model);
            }
            if (m_pSharedState->h_fov_scale > 0.1f && m_pSharedState->v_fov_scale > 0.1f) {
                float resScale = (m_pSharedState->render_width_per_eye > 0)
                    ? (static_cast<float>(m_pSharedState->render_width_per_eye) / 2064.0f)
                    : 1.0f;
                m_pHmd->SetOpticalConfig(m_pSharedState->h_fov_scale, m_pSharedState->v_fov_scale, resScale);
            }
            m_pHmd->UpdatePose(m_pSharedState->hmd_pose);
        }
        if (m_pLeftController) {
            m_pLeftController->UpdatePose(m_pSharedState->controller_pose[0]);
            m_pLeftController->UpdateInput(m_pSharedState->controller_input[0]);
        }
        if (m_pRightController) {
            m_pRightController->UpdatePose(m_pSharedState->controller_pose[1]);
            m_pRightController->UpdateInput(m_pSharedState->controller_input[1]);
        }
        return;
    }
#endif

    // Keep default valid poses alive if ProjectVXR.exe hasn't written a frame yet
    if (m_pHmd) {
        VxrPose6DoF defaultHmd = {};
        defaultHmd.position[0] = 0.0f;
        defaultHmd.position[1] = 1.65f;
        defaultHmd.position[2] = 0.0f;
        defaultHmd.orientation[3] = 1.0f;
        m_pHmd->UpdatePose(defaultHmd);
    }
    if (m_pLeftController) {
        VxrPose6DoF defaultLeft = {};
        defaultLeft.position[0] = -0.25f;
        defaultLeft.position[1] = 1.15f;
        defaultLeft.position[2] = -0.35f;
        defaultLeft.orientation[3] = 1.0f;
        m_pLeftController->UpdatePose(defaultLeft);
    }
    if (m_pRightController) {
        VxrPose6DoF defaultRight = {};
        defaultRight.position[0] = 0.25f;
        defaultRight.position[1] = 1.15f;
        defaultRight.position[2] = -0.35f;
        defaultRight.orientation[3] = 1.0f;
        m_pRightController->UpdatePose(defaultRight);
    }
}

bool VxrDriverServerProvider::ShouldBlockStandbyMode() {
    return true;
}

void VxrDriverServerProvider::EnterStandby() {
    if (m_pHmd) m_pHmd->EnterStandby();
    if (m_pLeftController) m_pLeftController->EnterStandby();
    if (m_pRightController) m_pRightController->EnterStandby();
}

void VxrDriverServerProvider::LeaveStandby() {
}

} // namespace vxr
