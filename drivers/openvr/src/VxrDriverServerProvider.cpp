#include "VxrDriverServerProvider.h"
#include <iostream>

namespace vxr {

static const char* const s_InterfaceVersions[] = {
    vr::IServerTrackedDeviceProvider_Version,
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

vr::EVRInitError VxrDriverServerProvider::Init(vr::IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    // Create virtual HMD and dual 6DoF controllers
    m_pHmd = std::make_unique<VxrHmdDevice>("VXR-HMD-001");
    m_pLeftController = std::make_unique<VxrControllerDevice>(0); // Left Hand
    m_pRightController = std::make_unique<VxrControllerDevice>(1); // Right Hand

    // Register devices with SteamVR vrserver host
    if (vr::VRServerDriverHost() != nullptr) {
        bool hmdAdded = vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pHmd->GetSerialNumber().c_str(),
            vr::TrackedDeviceClass_HMD,
            m_pHmd.get()
        );
        if (!hmdAdded) {
            std::cerr << "[VXR-OpenVR] Failed to add HMD to vrserver host!" << std::endl;
        }

        bool leftAdded = vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pLeftController->GetSerialNumber().c_str(),
            vr::TrackedDeviceClass_Controller,
            m_pLeftController.get()
        );
        if (!leftAdded) {
            std::cerr << "[VXR-OpenVR] Failed to add Left Controller to vrserver host!" << std::endl;
        }

        bool rightAdded = vr::VRServerDriverHost()->TrackedDeviceAdded(
            m_pRightController->GetSerialNumber().c_str(),
            vr::TrackedDeviceClass_Controller,
            m_pRightController.get()
        );
        if (!rightAdded) {
            std::cerr << "[VXR-OpenVR] Failed to add Right Controller to vrserver host!" << std::endl;
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
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
        m_initialized.store(false);
    }
}

const char* const* VxrDriverServerProvider::GetInterfaceVersions() {
    return s_InterfaceVersions;
}

void VxrDriverServerProvider::RunFrame() {
    // Process driver frame updates
}

bool VxrDriverServerProvider::ShouldBlockStandbyMode() {
    return false;
}

void VxrDriverServerProvider::EnterStandby() {
    if (m_pHmd) m_pHmd->EnterStandby();
    if (m_pLeftController) m_pLeftController->EnterStandby();
    if (m_pRightController) m_pRightController->EnterStandby();
}

void VxrDriverServerProvider::LeaveStandby() {
}

} // namespace vxr
