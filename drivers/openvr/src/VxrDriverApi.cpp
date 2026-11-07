#include "VxrDriverShared.h"
#include "VxrDriverServerProvider.h"

extern "C" {

bool VxrDriver_UpdateHmdPose(const VxrPose6DoF* pose) {
    if (!pose) return false;
    auto hmd = vxr::VxrDriverServerProvider::GetInstance().GetHmd();
    if (!hmd) return false;
    return hmd->UpdatePose(*pose);
}

bool VxrDriver_UpdateControllerPose(int hand, const VxrPose6DoF* pose) {
    if (!pose) return false;
    auto& provider = vxr::VxrDriverServerProvider::GetInstance();
    vxr::VxrControllerDevice* ctrl = (hand == 0) ? provider.GetLeftController() : provider.GetRightController();
    if (!ctrl) return false;
    return ctrl->UpdatePose(*pose);
}

bool VxrDriver_UpdateControllerInput(int hand, const VxrControllerInput* input) {
    if (!input) return false;
    auto& provider = vxr::VxrDriverServerProvider::GetInstance();
    vxr::VxrControllerDevice* ctrl = (hand == 0) ? provider.GetLeftController() : provider.GetRightController();
    if (!ctrl) return false;
    return ctrl->UpdateInput(*input);
}

bool VxrDriver_IsActive(void) {
    return vxr::VxrDriverServerProvider::GetInstance().IsInitialized();
}

__declspec(dllexport) void VxrDriver_SetOpticalConfig(float h_fov_scale, float v_fov_scale, float render_scale) {
    auto hmd = vxr::VxrDriverServerProvider::GetInstance().GetHmd();
    if (hmd) {
        hmd->SetOpticalConfig(h_fov_scale, v_fov_scale, render_scale);
    }
}

} // extern "C"
