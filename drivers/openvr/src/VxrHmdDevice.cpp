#include "VxrHmdDevice.h"
#include <cstring>
#include <iostream>

namespace vxr {

VxrHmdDevice::VxrHmdDevice(const std::string& serialNumber, const std::string& modelName)
    : m_serialNumber(serialNumber)
    , m_modelName(modelName.empty() ? "Meta Quest 2" : modelName)
    , m_objectId(vr::k_unTrackedDeviceIndexInvalid)
    , m_propertyContainer(vr::k_ulInvalidPropertyContainer)
    , m_submittedFrames(0)
    , m_lastSubmittedLeftEye(0)
    , m_lastSubmittedRightEye(0)
    , m_renderWidthPerEye(2064)
    , m_renderHeightPerEye(2208)
    , m_displayFrequency(90.0f)
    , m_ipdMeters(0.063f)
{
    memset(&m_currentPose, 0, sizeof(m_currentPose));
    m_currentPose.poseIsValid = true;
    m_currentPose.result = vr::TrackingResult_Running_OK;
    m_currentPose.deviceIsConnected = true;
    m_currentPose.qWorldFromDriverRotation.w = 1.0;
    m_currentPose.qDriverFromHeadRotation.w = 1.0;
    m_currentPose.qRotation.w = 1.0;
    m_currentPose.vecPosition[1] = 1.65; // Standing head height default (1.65m)
}

VxrHmdDevice::~VxrHmdDevice() {
    Deactivate();
}

void VxrHmdDevice::UpdateModelName(const std::string& modelName) {
    if (modelName.empty() || modelName == m_modelName) return;
    m_modelName = modelName;
    if (m_propertyContainer != vr::k_ulInvalidPropertyContainer && vr::VRProperties() != nullptr) {
        vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ModelNumber_String, m_modelName.c_str());
    }
}

vr::EVRInitError VxrHmdDevice::Activate(uint32_t unObjectId) {
    m_objectId = unObjectId;
    m_propertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_objectId);

    // Set standard OpenVR HMD properties (matching SteamVR 2.18.4 Virtual/Null HMD requirements)
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ModelNumber_String, m_modelName.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_RenderModelName_String, m_modelName.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_SerialNumber_String, m_serialNumber.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ManufacturerName_String, "Oculus");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_TrackingSystemName_String, "oculus");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_RegisteredDeviceType_String, "oculus/VXR-HMD-001");

    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_UserIpdMeters_Float, m_ipdMeters);
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.0f);
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_DisplayFrequency_Float, m_displayFrequency);
    vr::VRProperties()->SetFloatProperty(m_propertyContainer, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.0111111f);

    vr::VRProperties()->SetUint64Property(m_propertyContainer, vr::Prop_CurrentUniverseId_Uint64, 2);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_ContainsProximitySensor_Bool, true);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_NeverTracked_Bool, false);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_DeviceCanPowerOff_Bool, false);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_CanUnifyCoordinateSystemWithHmd_Bool, true);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_Firmware_ForceUpdateRequired_Bool, false);

    // Critical for SteamVR 2.18.4 Virtual HMD Compositor:
    // Prop_DisplayDebugMode_Bool = true + Prop_IsOnDesktop_Bool = false tells vrcompositor.exe
    // to run an offscreen/virtual compositor swapchain without requiring physical HDMI/DP EDID!
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_DisplayDebugMode_Bool, true);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_IsOnDesktop_Bool, false);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_HasDisplayComponent_Bool, true);
    vr::VRProperties()->SetBoolProperty(m_propertyContainer, vr::Prop_ReportsTimeSinceVSync_Bool, false);

    // Register & assert /proximity and /user/head/proximity sensors active so SteamVR & OVR Toolkit immediately exit Standby!
    if (vr::VRDriverInput() != nullptr) {
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/proximity", &m_hProximity);
        if (m_hProximity != vr::k_ulInvalidInputComponentHandle) {
            vr::VRDriverInput()->UpdateBooleanComponent(m_hProximity, true, 0.0);
        }
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/user/head/proximity", &m_hUserHeadProximity);
        if (m_hUserHeadProximity != vr::k_ulInvalidInputComponentHandle) {
            vr::VRDriverInput()->UpdateBooleanComponent(m_hUserHeadProximity, true, 0.0);
        }
    }

    // Publish initial valid pose immediately upon activation
    if (vr::VRServerDriverHost() != nullptr) {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_objectId, m_currentPose, sizeof(m_currentPose));
    }

    return vr::VRInitError_None;
}

void VxrHmdDevice::Deactivate() {
    m_objectId = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
}

void VxrHmdDevice::EnterStandby() {
}

void* VxrHmdDevice::GetComponent(const char* pchComponentNameAndVersion) {
    if (pchComponentNameAndVersion == nullptr) {
        return nullptr;
    }

    if (strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version) == 0) {
        return static_cast<vr::IVRDisplayComponent*>(this);
    }

    // Only expose IVRDriverDirectModeComponent when explicitly running offline unit tests
    // (when vr::VRDriverContext() == nullptr). When running inside real SteamVR vrserver.exe,
    // returning nullptr here lets vrcompositor.exe use IVRDisplayComponent in virtual display mode!
    if (strcmp(pchComponentNameAndVersion, vr::IVRDriverDirectModeComponent_Version) == 0) {
        if (vr::VRDriverContext() == nullptr) {
            return static_cast<vr::IVRDriverDirectModeComponent*>(this);
        }
        return nullptr;
    }

    return nullptr;
}

void VxrHmdDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) {
    if (unResponseBufferSize > 0 && pchResponseBuffer != nullptr) {
        pchResponseBuffer[0] = 0;
    }
}

vr::DriverPose_t VxrHmdDevice::GetPose() {
    std::lock_guard<std::mutex> lock(m_poseMutex);
    return m_currentPose;
}

bool VxrHmdDevice::UpdatePose(const VxrPose6DoF& pose) {
    std::lock_guard<std::mutex> lock(m_poseMutex);

    m_currentPose.vecPosition[0] = pose.position[0];
    m_currentPose.vecPosition[1] = pose.position[1];
    m_currentPose.vecPosition[2] = pose.position[2];

    m_currentPose.qRotation.x = pose.orientation[0];
    m_currentPose.qRotation.y = pose.orientation[1];
    m_currentPose.qRotation.z = pose.orientation[2];
    m_currentPose.qRotation.w = pose.orientation[3];
    if (m_currentPose.qRotation.x == 0.0 && m_currentPose.qRotation.y == 0.0 &&
        m_currentPose.qRotation.z == 0.0 && m_currentPose.qRotation.w == 0.0) {
        m_currentPose.qRotation.w = 1.0;
    }

    m_currentPose.vecVelocity[0] = pose.linear_velocity[0];
    m_currentPose.vecVelocity[1] = pose.linear_velocity[1];
    m_currentPose.vecVelocity[2] = pose.linear_velocity[2];

    m_currentPose.vecAngularVelocity[0] = pose.angular_velocity[0];
    m_currentPose.vecAngularVelocity[1] = pose.angular_velocity[1];
    m_currentPose.vecAngularVelocity[2] = pose.angular_velocity[2];

    m_currentPose.poseIsValid = true;
    m_currentPose.deviceIsConnected = true;
    m_currentPose.result = vr::TrackingResult_Running_OK;

    if (m_objectId != vr::k_unTrackedDeviceIndexInvalid && vr::VRServerDriverHost() != nullptr) {
        if (vr::VRDriverInput() != nullptr) {
            if (m_hProximity != vr::k_ulInvalidInputComponentHandle) {
                vr::VRDriverInput()->UpdateBooleanComponent(m_hProximity, true, 0.0);
            }
            if (m_hUserHeadProximity != vr::k_ulInvalidInputComponentHandle) {
                vr::VRDriverInput()->UpdateBooleanComponent(m_hUserHeadProximity, true, 0.0);
            }
        }
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_objectId, m_currentPose, sizeof(m_currentPose));
        return true;
    }

    return false;
}

// =============================================================================
// IVRDisplayComponent
// =============================================================================

void VxrHmdDevice::GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) {
    if (pnX) *pnX = 0;
    if (pnY) *pnY = 0;
    if (pnWidth) *pnWidth = 1920;
    if (pnHeight) *pnHeight = 1080;
}

bool VxrHmdDevice::IsDisplayOnDesktop() {
    return false;
}

bool VxrHmdDevice::IsDisplayRealDisplay() {
    return false;
}

void VxrHmdDevice::GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) {
    if (pnWidth) *pnWidth = m_renderWidthPerEye;
    if (pnHeight) *pnHeight = m_renderHeightPerEye;
}

void VxrHmdDevice::GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) {
    if (pnY) *pnY = 0;
    if (pnWidth) *pnWidth = 1920 / 2;
    if (pnHeight) *pnHeight = 1080;

    if (eEye == vr::Eye_Left) {
        if (pnX) *pnX = 0;
    } else {
        if (pnX) *pnX = 1920 / 2;
    }
}

void VxrHmdDevice::GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) {
    if (pfLeft) *pfLeft = -1.05f * m_hFovScale;
    if (pfRight) *pfRight = 1.05f * m_hFovScale;
    if (pfTop) *pfTop = -1.05f * m_vFovScale;
    if (pfBottom) *pfBottom = 1.05f * m_vFovScale;
}

void VxrHmdDevice::SetOpticalConfig(float hFovScale, float vFovScale, float renderScale) {
    m_hFovScale = hFovScale;
    m_vFovScale = vFovScale;
    m_renderWidthPerEye = static_cast<uint32_t>(2064.0f * renderScale);
    m_renderHeightPerEye = static_cast<uint32_t>(2208.0f * renderScale);
}

vr::DistortionCoordinates_t VxrHmdDevice::ComputeDistortion(vr::EVREye eEye, float fU, float fV) {
    vr::DistortionCoordinates_t coords;
    coords.rfRed[0] = fU;
    coords.rfRed[1] = fV;
    coords.rfGreen[0] = fU;
    coords.rfGreen[1] = fV;
    coords.rfBlue[0] = fU;
    coords.rfBlue[1] = fV;
    return coords;
}

bool VxrHmdDevice::ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV) {
    if (pResult) {
        pResult->v[0] = fU;
        pResult->v[1] = fV;
        return true;
    }
    return false;
}

// =============================================================================
// IVRDriverDirectModeComponent
// =============================================================================

void VxrHmdDevice::CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t* pSwapTextureSetDesc, SwapTextureSet_t* pOutSwapTextureSet) {
    if (!pOutSwapTextureSet) return;

    for (int i = 0; i < 3; ++i) {
        pOutSwapTextureSet->rSharedTextureHandles[i] = (vr::SharedTextureHandle_t)(uintptr_t)(0x10000 + i * 0x1000 + unPid);
    }
    pOutSwapTextureSet->unTextureFlags = 0;
}

void VxrHmdDevice::DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle) {
}

void VxrHmdDevice::DestroyAllSwapTextureSets(uint32_t unPid) {
}

void VxrHmdDevice::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2]) {
    if (pIndices) {
        (*pIndices)[0] = 0;
        (*pIndices)[1] = 0;
    }
}

void VxrHmdDevice::SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2]) {
    std::lock_guard<std::mutex> lock(m_directModeMutex);
    m_lastSubmittedLeftEye = perEye[0].hTexture;
    m_lastSubmittedRightEye = perEye[1].hTexture;
    m_submittedFrames++;
}

void VxrHmdDevice::Present(vr::SharedTextureHandle_t syncTexture) {
}

void VxrHmdDevice::PostPresent(const Throttling_t* pThrottling) {
}

void VxrHmdDevice::GetFrameTiming(vr::DriverDirectMode_FrameTiming* pFrameTiming) {
    if (pFrameTiming) {
        pFrameTiming->m_nReprojectionFlags = 0;
    }
}

} // namespace vxr
