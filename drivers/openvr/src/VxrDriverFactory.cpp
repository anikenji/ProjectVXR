#include "openvr_driver.h"
#include "VxrDriverServerProvider.h"
#include <cstring>

#if defined(_WIN32)
#define HMD_DLL_EXPORT __declspec(dllexport)
#else
#define HMD_DLL_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HMD_DLL_EXPORT void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode) {
    if (pInterfaceName == nullptr) {
        if (pReturnCode) *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }

    if (strcmp(pInterfaceName, vr::IServerTrackedDeviceProvider_Version) == 0) {
        if (pReturnCode) *pReturnCode = vr::VRInitError_None;
        return static_cast<vr::IServerTrackedDeviceProvider*>(&vxr::VxrDriverServerProvider::GetInstance());
    }

    if (pReturnCode) *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
