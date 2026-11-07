#ifndef VXR_HMD_DEVICE_H
#define VXR_HMD_DEVICE_H

#include "openvr_driver.h"
#include "VxrDriverShared.h"
#include <string>
#include <mutex>

namespace vxr {

class VxrHmdDevice : public vr::ITrackedDeviceServerDriver,
                     public vr::IVRDisplayComponent,
                     public vr::IVRDriverDirectModeComponent
{
public:
    VxrHmdDevice(const std::string& serialNumber = "VXR-HMD-001");
    virtual ~VxrHmdDevice();

    // =========================================================================
    // ITrackedDeviceServerDriver interface
    // =========================================================================
    virtual vr::EVRInitError Activate(uint32_t unObjectId) override;
    virtual void Deactivate() override;
    virtual void EnterStandby() override;
    virtual void* GetComponent(const char* pchComponentNameAndVersion) override;
    virtual void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    virtual vr::DriverPose_t GetPose() override;

    // =========================================================================
    // IVRDisplayComponent interface
    // =========================================================================
    virtual void GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override;
    virtual bool IsDisplayOnDesktop() override;
    virtual bool IsDisplayRealDisplay() override;
    virtual void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) override;
    virtual void GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override;
    virtual void GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) override;
    virtual vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV) override;
    virtual bool ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV) override;

    // =========================================================================
    // IVRDriverDirectModeComponent interface
    // =========================================================================
    virtual void CreateSwapTextureSet(uint32_t unPid, const SwapTextureSetDesc_t* pSwapTextureSetDesc, SwapTextureSet_t* pOutSwapTextureSet) override;
    virtual void DestroySwapTextureSet(vr::SharedTextureHandle_t sharedTextureHandle) override;
    virtual void DestroyAllSwapTextureSets(uint32_t unPid) override;
    virtual void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t sharedTextureHandles[2], uint32_t (*pIndices)[2]) override;
    virtual void SubmitLayer(const SubmitLayerPerEye_t (&perEye)[2]) override;
    virtual void Present(vr::SharedTextureHandle_t syncTexture) override;
    virtual void PostPresent(const Throttling_t* pThrottling) override;
    virtual void GetFrameTiming(vr::DriverDirectMode_FrameTiming* pFrameTiming) override;

    // =========================================================================
    // VXR Custom Methods
    // =========================================================================
    bool UpdatePose(const VxrPose6DoF& pose);
    uint32_t GetObjectId() const { return m_objectId; }
    const std::string& GetSerialNumber() const { return m_serialNumber; }
    bool IsActive() const { return m_objectId != vr::k_unTrackedDeviceIndexInvalid; }

    uint64_t GetLastSubmittedFrameIndex() const { return m_submittedFrames; }
    vr::SharedTextureHandle_t GetLastSubmittedTexture(vr::EVREye eye) const {
        return (eye == vr::Eye_Left) ? m_lastSubmittedLeftEye : m_lastSubmittedRightEye;
    }

private:
    std::string m_serialNumber;
    uint32_t m_objectId;
    vr::PropertyContainerHandle_t m_propertyContainer;

    std::mutex m_poseMutex;
    vr::DriverPose_t m_currentPose;

    // Direct mode state
    std::mutex m_directModeMutex;
    uint64_t m_submittedFrames;
    vr::SharedTextureHandle_t m_lastSubmittedLeftEye;
    vr::SharedTextureHandle_t m_lastSubmittedRightEye;

    // Display configuration: Quest 3 dual panels
    uint32_t m_renderWidthPerEye;
    uint32_t m_renderHeightPerEye;
    float m_displayFrequency;
    float m_ipdMeters;
};

} // namespace vxr

#endif // VXR_HMD_DEVICE_H
