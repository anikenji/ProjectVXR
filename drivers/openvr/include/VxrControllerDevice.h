#ifndef VXR_CONTROLLER_DEVICE_H
#define VXR_CONTROLLER_DEVICE_H

#include "openvr_driver.h"
#include "VxrDriverShared.h"
#include <string>
#include <mutex>

namespace vxr {

class VxrControllerDevice : public vr::ITrackedDeviceServerDriver {
public:
    VxrControllerDevice(int handRole); // 0 = Left Hand, 1 = Right Hand
    virtual ~VxrControllerDevice();

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
    // VXR Custom Methods
    // =========================================================================
    bool UpdatePose(const VxrPose6DoF& pose);
    bool UpdateInput(const VxrControllerInput& input);

    uint32_t GetObjectId() const { return m_objectId; }
    int GetHandRole() const { return m_handRole; }
    const std::string& GetSerialNumber() const { return m_serialNumber; }
    bool IsActive() const { return m_objectId != vr::k_unTrackedDeviceIndexInvalid; }

private:
    int m_handRole; // 0 = Left, 1 = Right
    std::string m_serialNumber;
    uint32_t m_objectId;
    vr::PropertyContainerHandle_t m_propertyContainer;

    std::mutex m_poseMutex;
    vr::DriverPose_t m_currentPose;

    // OpenVR Input Component Handles
    vr::VRInputComponentHandle_t m_hTriggerPull;
    vr::VRInputComponentHandle_t m_hTriggerClick;
    vr::VRInputComponentHandle_t m_hGripPull;
    vr::VRInputComponentHandle_t m_hGripClick;
    vr::VRInputComponentHandle_t m_hJoystickX;
    vr::VRInputComponentHandle_t m_hJoystickY;
    vr::VRInputComponentHandle_t m_hJoystickClick;
    vr::VRInputComponentHandle_t m_hButtonA_or_X;
    vr::VRInputComponentHandle_t m_hButtonB_or_Y;
    vr::VRInputComponentHandle_t m_hMenuButton;
    vr::VRInputComponentHandle_t m_hHaptic;

    VxrControllerInput m_lastInput;
};

} // namespace vxr

#endif // VXR_CONTROLLER_DEVICE_H
