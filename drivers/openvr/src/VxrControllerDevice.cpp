#include "VxrControllerDevice.h"
#include <cstring>
#include <iostream>

namespace vxr {

VxrControllerDevice::VxrControllerDevice(int handRole)
    : m_handRole(handRole)
    , m_serialNumber(handRole == 0 ? "VXR-CTRL-LEFT" : "VXR-CTRL-RIGHT")
    , m_objectId(vr::k_unTrackedDeviceIndexInvalid)
    , m_propertyContainer(vr::k_ulInvalidPropertyContainer)
    , m_hTriggerPull(vr::k_ulInvalidInputComponentHandle)
    , m_hTriggerClick(vr::k_ulInvalidInputComponentHandle)
    , m_hGripPull(vr::k_ulInvalidInputComponentHandle)
    , m_hGripClick(vr::k_ulInvalidInputComponentHandle)
    , m_hJoystickX(vr::k_ulInvalidInputComponentHandle)
    , m_hJoystickY(vr::k_ulInvalidInputComponentHandle)
    , m_hJoystickClick(vr::k_ulInvalidInputComponentHandle)
    , m_hButtonA_or_X(vr::k_ulInvalidInputComponentHandle)
    , m_hButtonB_or_Y(vr::k_ulInvalidInputComponentHandle)
    , m_hMenuButton(vr::k_ulInvalidInputComponentHandle)
    , m_hHaptic(vr::k_ulInvalidInputComponentHandle)
{
    memset(&m_currentPose, 0, sizeof(m_currentPose));
    m_currentPose.poseIsValid = true;
    m_currentPose.result = vr::TrackingResult_Running_OK;
    m_currentPose.deviceIsConnected = true;
    m_currentPose.qWorldFromDriverRotation.w = 1.0;
    m_currentPose.qDriverFromHeadRotation.w = 1.0;
    m_currentPose.qRotation.w = 1.0;
    m_currentPose.vecPosition[0] = (handRole == 0) ? -0.25 : 0.25;
    m_currentPose.vecPosition[1] = 1.15;
    m_currentPose.vecPosition[2] = -0.35;

    memset(&m_lastInput, 0, sizeof(m_lastInput));
}

VxrControllerDevice::~VxrControllerDevice() {
    Deactivate();
}

vr::EVRInitError VxrControllerDevice::Activate(uint32_t unObjectId) {
    m_objectId = unObjectId;
    m_propertyContainer = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_objectId);

    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ModelNumber_String, m_handRole == 0 ? "Oculus Quest2 (Left Controller)" : "Oculus Quest2 (Right Controller)");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_RenderModelName_String, m_handRole == 0 ? "oculus_quest2_controller_left" : "oculus_quest2_controller_right");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_SerialNumber_String, m_serialNumber.c_str());
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ManufacturerName_String, "Oculus");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_TrackingSystemName_String, "oculus");
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_ControllerType_String, "oculus_touch");

    vr::ETrackedControllerRole role = (m_handRole == 0) ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    vr::VRProperties()->SetInt32Property(m_propertyContainer, vr::Prop_ControllerRoleHint_Int32, role);
    vr::VRProperties()->SetInt32Property(m_propertyContainer, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);
    vr::VRProperties()->SetStringProperty(m_propertyContainer, vr::Prop_InputProfilePath_String, "{oculus}/input/touch_profile.json");

    if (vr::VRServerDriverHost() != nullptr) {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_objectId, m_currentPose, sizeof(m_currentPose));
    }

    // Register OpenVR Input Components
    if (vr::VRDriverInput() != nullptr) {
        vr::VRDriverInput()->CreateScalarComponent(
            m_propertyContainer,
            "/input/trigger/value",
            &m_hTriggerPull,
            vr::VRScalarType_Absolute,
            vr::VRScalarUnits_NormalizedOneSided
        );
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/trigger/click", &m_hTriggerClick);

        vr::VRDriverInput()->CreateScalarComponent(
            m_propertyContainer,
            "/input/grip/value",
            &m_hGripPull,
            vr::VRScalarType_Absolute,
            vr::VRScalarUnits_NormalizedOneSided
        );
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/grip/click", &m_hGripClick);

        vr::VRDriverInput()->CreateScalarComponent(
            m_propertyContainer,
            "/input/thumbstick/x",
            &m_hJoystickX,
            vr::VRScalarType_Absolute,
            vr::VRScalarUnits_NormalizedTwoSided
        );
        vr::VRDriverInput()->CreateScalarComponent(
            m_propertyContainer,
            "/input/thumbstick/y",
            &m_hJoystickY,
            vr::VRScalarType_Absolute,
            vr::VRScalarUnits_NormalizedTwoSided
        );
        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/thumbstick/click", &m_hJoystickClick);

        if (m_handRole == 0) {
            vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/x/click", &m_hButtonA_or_X);
            vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/y/click", &m_hButtonB_or_Y);
        } else {
            vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/a/click", &m_hButtonA_or_X);
            vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/b/click", &m_hButtonB_or_Y);
        }

        vr::VRDriverInput()->CreateBooleanComponent(m_propertyContainer, "/input/menu/click", &m_hMenuButton);
        vr::VRDriverInput()->CreateHapticComponent(m_propertyContainer, "/output/haptic", &m_hHaptic);
    }

    return vr::VRInitError_None;
}

void VxrControllerDevice::Deactivate() {
    m_objectId = vr::k_unTrackedDeviceIndexInvalid;
    m_propertyContainer = vr::k_ulInvalidPropertyContainer;
}

void VxrControllerDevice::EnterStandby() {
}

void* VxrControllerDevice::GetComponent(const char* pchComponentNameAndVersion) {
    return nullptr;
}

void VxrControllerDevice::DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) {
    if (unResponseBufferSize > 0 && pchResponseBuffer != nullptr) {
        pchResponseBuffer[0] = 0;
    }
}

vr::DriverPose_t VxrControllerDevice::GetPose() {
    std::lock_guard<std::mutex> lock(m_poseMutex);
    return m_currentPose;
}

bool VxrControllerDevice::UpdatePose(const VxrPose6DoF& pose) {
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
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_objectId, m_currentPose, sizeof(m_currentPose));
        return true;
    }

    return false;
}

bool VxrControllerDevice::UpdateInput(const VxrControllerInput& input) {
    m_lastInput = input;

    if (m_objectId == vr::k_unTrackedDeviceIndexInvalid || vr::VRDriverInput() == nullptr) {
        return false;
    }

    if (m_hTriggerPull != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateScalarComponent(m_hTriggerPull, input.trigger_value, 0.0);
    }
    if (m_hGripPull != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateScalarComponent(m_hGripPull, input.grip_value, 0.0);
    }
    if (m_hJoystickX != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateScalarComponent(m_hJoystickX, input.joystick[0], 0.0);
    }
    if (m_hJoystickY != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateScalarComponent(m_hJoystickY, input.joystick[1], 0.0);
    }

    bool triggerClick = (input.buttons & VXR_BTN_TRIGGER_CLICK) != 0;
    bool gripClick = (input.buttons & VXR_BTN_GRIP_CLICK) != 0;
    bool btnA_X = (input.buttons & VXR_BTN_A_X) != 0;
    bool btnB_Y = (input.buttons & VXR_BTN_B_Y) != 0;
    bool stickClick = (input.buttons & VXR_BTN_STICK_CLICK) != 0;
    bool menu = (input.buttons & VXR_BTN_MENU) != 0;

    if (m_hTriggerClick != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(m_hTriggerClick, triggerClick, 0.0);
    }
    if (m_hGripClick != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(m_hGripClick, gripClick, 0.0);
    }
    if (m_hButtonA_or_X != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(m_hButtonA_or_X, btnA_X, 0.0);
    }
    if (m_hButtonB_or_Y != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(m_hButtonB_or_Y, btnB_Y, 0.0);
    }
    if (m_hJoystickClick != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(m_hJoystickClick, stickClick, 0.0);
    }
    if (m_hMenuButton != vr::k_ulInvalidInputComponentHandle) {
        vr::VRDriverInput()->UpdateBooleanComponent(m_hMenuButton, menu, 0.0);
    }

    return true;
}

} // namespace vxr
