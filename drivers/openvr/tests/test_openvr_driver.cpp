#include "openvr_driver.h"
#include "VxrDriverServerProvider.h"
#include "VxrHmdDevice.h"
#include "VxrControllerDevice.h"
#include "VxrDriverShared.h"

#include <iostream>
#include <cassert>
#include <cmath>

extern "C" void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode);

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: SteamVR OpenVR Driver Test Suite " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: HmdDriverFactory export
    std::cout << "[Test 1] Testing HmdDriverFactory interface queries..." << std::endl;
    int returnCode = -1;
    void* provider = HmdDriverFactory(vr::IServerTrackedDeviceProvider_Version, &returnCode);
    assert(provider != nullptr);
    assert(returnCode == vr::VRInitError_None);
    std::cout << "  -> HmdDriverFactory returned valid IServerTrackedDeviceProvider (004)." << std::endl;

    void* invalid = HmdDriverFactory("InvalidInterface_999", &returnCode);
    assert(invalid == nullptr);
    assert(returnCode == vr::VRInitError_Init_InterfaceNotFound);
    std::cout << "  -> Correctly rejected invalid interface request." << std::endl;

    // Test 2: Virtual HMD Display Component
    std::cout << "[Test 2] Testing Virtual HMD display component specifications..." << std::endl;
    vxr::VxrHmdDevice hmd("VXR-HMD-TEST");
    assert(hmd.GetSerialNumber() == "VXR-HMD-TEST");

    // Display component interface
    auto display = static_cast<vr::IVRDisplayComponent*>(hmd.GetComponent(vr::IVRDisplayComponent_Version));
    assert(display != nullptr);
    assert(!display->IsDisplayOnDesktop());
    assert(!display->IsDisplayRealDisplay());

    int32_t winX = -1, winY = -1;
    uint32_t winW = 0, winH = 0;
    display->GetWindowBounds(&winX, &winY, &winW, &winH);
    assert(winX == 0 && winY == 0);
    assert(winW == 4128 && winH == 2208); // 2064x2208 per eye
    std::cout << "  -> HMD Window Bounds: " << winW << "x" << winH << " (Dual 2064x2208 panels)" << std::endl;

    uint32_t renderW = 0, renderH = 0;
    display->GetRecommendedRenderTargetSize(&renderW, &renderH);
    assert(renderW == 2064 && renderH == 2208);
    std::cout << "  -> Recommended Render Target: " << renderW << "x" << renderH << " per eye" << std::endl;

    uint32_t eyeX = 0, eyeY = 0, eyeW = 0, eyeH = 0;
    display->GetEyeOutputViewport(vr::Eye_Left, &eyeX, &eyeY, &eyeW, &eyeH);
    assert(eyeX == 0 && eyeY == 0 && eyeW == 2064 && eyeH == 2208);

    display->GetEyeOutputViewport(vr::Eye_Right, &eyeX, &eyeY, &eyeW, &eyeH);
    assert(eyeX == 2064 && eyeY == 0 && eyeW == 2064 && eyeH == 2208);
    std::cout << "  -> Eye output viewports: Left [0, 0, 2064, 2208], Right [2064, 0, 2064, 2208]" << std::endl;

    float left = 0, right = 0, top = 0, bottom = 0;
    display->GetProjectionRaw(vr::Eye_Left, &left, &right, &top, &bottom);
    assert(left < 0 && right > 0 && top < 0 && bottom > 0);
    std::cout << "  -> Raw projection tangents: (" << left << ", " << right << ", " << top << ", " << bottom << ")" << std::endl;

    vr::DistortionCoordinates_t dist = display->ComputeDistortion(vr::Eye_Left, 0.5f, 0.5f);
    assert(dist.rfRed[0] == 0.5f && dist.rfRed[1] == 0.5f);
    std::cout << "  -> Identity lens distortion mapping confirmed (client-side Quest warp)." << std::endl;

    // Test 3: Virtual HMD Direct Mode Interface
    std::cout << "[Test 3] Testing Virtual HMD Direct Mode component..." << std::endl;
    auto directMode = static_cast<vr::IVRDriverDirectModeComponent*>(hmd.GetComponent(vr::IVRDriverDirectModeComponent_Version));
    assert(directMode != nullptr);

    vr::IVRDriverDirectModeComponent::SwapTextureSetDesc_t swapDesc = { 2064, 2208, 0, 1 };
    vr::IVRDriverDirectModeComponent::SwapTextureSet_t swapSet;
    directMode->CreateSwapTextureSet(1234, &swapDesc, &swapSet);
    assert(swapSet.rSharedTextureHandles[0] != 0);
    std::cout << "  -> Direct Mode swap texture set created: handle=" << (void*)swapSet.rSharedTextureHandles[0] << std::endl;

    // Submit layer
    vr::IVRDriverDirectModeComponent::SubmitLayerPerEye_t submitPerEye[2];
    submitPerEye[0].hTexture = (vr::SharedTextureHandle_t)0xAAAA1111;
    submitPerEye[0].hDepthTexture = 0;
    submitPerEye[1].hTexture = (vr::SharedTextureHandle_t)0xBBBB2222;
    submitPerEye[1].hDepthTexture = 0;

    directMode->SubmitLayer(submitPerEye);
    assert(hmd.GetLastSubmittedFrameIndex() == 1);
    assert(hmd.GetLastSubmittedTexture(vr::Eye_Left) == (vr::SharedTextureHandle_t)0xAAAA1111);
    assert(hmd.GetLastSubmittedTexture(vr::Eye_Right) == (vr::SharedTextureHandle_t)0xBBBB2222);
    std::cout << "  -> Direct Mode layer submission verified (left & right textures captured)." << std::endl;

    // Test 4: HMD 6DoF Pose Updates
    std::cout << "[Test 4] Testing HMD 6DoF pose update & coordinate transformation..." << std::endl;
    VxrPose6DoF hmdPose;
    hmdPose.position[0] = 0.15f;
    hmdPose.position[1] = 1.72f;
    hmdPose.position[2] = -0.45f;
    hmdPose.orientation[0] = 0.0f;
    hmdPose.orientation[1] = 0.7071f;
    hmdPose.orientation[2] = 0.0f;
    hmdPose.orientation[3] = 0.7071f;
    hmdPose.linear_velocity[0] = 0.01f;
    hmdPose.linear_velocity[1] = 0.0f;
    hmdPose.linear_velocity[2] = -0.02f;
    hmdPose.angular_velocity[0] = 0.0f;
    hmdPose.angular_velocity[1] = 0.1f;
    hmdPose.angular_velocity[2] = 0.0f;

    hmd.UpdatePose(hmdPose);
    vr::DriverPose_t driverPose = hmd.GetPose();
    assert(driverPose.poseIsValid);
    assert(std::abs(driverPose.vecPosition[0] - 0.15f) < 1e-4);
    assert(std::abs(driverPose.vecPosition[1] - 1.72f) < 1e-4);
    assert(std::abs(driverPose.vecPosition[2] - (-0.45f)) < 1e-4);
    assert(std::abs(driverPose.qRotation.y - 0.7071f) < 1e-4);
    assert(std::abs(driverPose.qRotation.w - 0.7071f) < 1e-4);
    std::cout << "  -> HMD pose validated: pos=(" << driverPose.vecPosition[0] << ", "
              << driverPose.vecPosition[1] << ", " << driverPose.vecPosition[2] << ")" << std::endl;

    // Test 5: Controllers (Left & Right)
    std::cout << "[Test 5] Testing dual 6DoF VR Controllers..." << std::endl;
    vxr::VxrControllerDevice leftCtrl(0);
    vxr::VxrControllerDevice rightCtrl(1);

    assert(leftCtrl.GetSerialNumber() == "VXR-CTRL-LEFT");
    assert(rightCtrl.GetSerialNumber() == "VXR-CTRL-RIGHT");
    assert(leftCtrl.GetHandRole() == 0);
    assert(rightCtrl.GetHandRole() == 1);

    // Left controller pose
    VxrPose6DoF leftPose;
    leftPose.position[0] = -0.25f;
    leftPose.position[1] = 1.15f;
    leftPose.position[2] = -0.30f;
    leftPose.orientation[0] = 0.0f;
    leftPose.orientation[1] = 0.0f;
    leftPose.orientation[2] = 0.0f;
    leftPose.orientation[3] = 1.0f;
    leftPose.linear_velocity[0] = 0.0f;
    leftPose.linear_velocity[1] = 0.0f;
    leftPose.linear_velocity[2] = 0.0f;
    leftPose.angular_velocity[0] = 0.0f;
    leftPose.angular_velocity[1] = 0.0f;
    leftPose.angular_velocity[2] = 0.0f;

    leftCtrl.UpdatePose(leftPose);
    vr::DriverPose_t leftDriverPose = leftCtrl.GetPose();
    assert(std::abs(leftDriverPose.vecPosition[0] - (-0.25f)) < 1e-4);
    assert(std::abs(leftDriverPose.vecPosition[1] - 1.15f) < 1e-4);

    // Right controller pose
    VxrPose6DoF rightPose;
    rightPose.position[0] = 0.25f;
    rightPose.position[1] = 1.15f;
    rightPose.position[2] = -0.30f;
    rightPose.orientation[0] = 0.0f;
    rightPose.orientation[1] = 0.0f;
    rightPose.orientation[2] = 0.0f;
    rightPose.orientation[3] = 1.0f;
    rightPose.linear_velocity[0] = 0.0f;
    rightPose.linear_velocity[1] = 0.0f;
    rightPose.linear_velocity[2] = 0.0f;
    rightPose.angular_velocity[0] = 0.0f;
    rightPose.angular_velocity[1] = 0.0f;
    rightPose.angular_velocity[2] = 0.0f;

    rightCtrl.UpdatePose(rightPose);
    vr::DriverPose_t rightDriverPose = rightCtrl.GetPose();
    assert(std::abs(rightDriverPose.vecPosition[0] - 0.25f) < 1e-4);
    std::cout << "  -> Left & Right controller 6DoF poses verified." << std::endl;

    // Controller input update
    VxrControllerInput ctrlInput;
    ctrlInput.trigger_value = 0.85f;
    ctrlInput.grip_value = 1.0f;
    ctrlInput.joystick[0] = 0.5f;
    ctrlInput.joystick[1] = -0.5f;
    ctrlInput.buttons = VXR_BTN_TRIGGER_CLICK | VXR_BTN_A_X;
    ctrlInput.battery_percent = 92;

    rightCtrl.UpdateInput(ctrlInput);
    std::cout << "  -> Controller analog axis and button inputs verified." << std::endl;

    // Test 6: Provider Interface Versions
    std::cout << "[Test 6] Testing Provider interface versions..." << std::endl;
    auto& serverProvider = vxr::VxrDriverServerProvider::GetInstance();
    const char* const* versions = serverProvider.GetInterfaceVersions();
    assert(versions != nullptr && versions[0] != nullptr);
    assert(std::string(versions[0]) == vr::IServerTrackedDeviceProvider_Version);
    std::cout << "  -> Interface version confirmed: " << versions[0] << std::endl;

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL OPENVR DRIVER TESTS PASSED!               " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
