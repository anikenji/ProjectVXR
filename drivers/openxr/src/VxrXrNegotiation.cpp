#include "VxrXrNegotiation.h"
#include "VxrXrRuntime.h"
#include <cstring>
#include <iostream>

#define ROUTE_OPENXR_FUNC(funcName)     if (strcmp(name, #funcName) == 0) {         *function = reinterpret_cast<PFN_xrVoidFunction>(funcName);         return XR_SUCCESS;     }

// Global OpenXR Entry Points (exported by runtime)
extern "C" {

__declspec(dllexport) void vxr_xr_set_optical_config(float h_fov_scale, float v_fov_scale, float render_scale, float fovea_ratio, int use_fov_stencil) {
    vxr::VxrXrRuntime::GetInstance().SetOpticalConfig(h_fov_scale, v_fov_scale, render_scale, fovea_ratio, use_fov_stencil != 0);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* createInfo, XrInstance* instance) {
    return vxr::VxrXrRuntime::GetInstance().CreateInstance(createInfo, instance);
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {
    return vxr::VxrXrRuntime::GetInstance().DestroyInstance(instance);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance instance, XrInstanceProperties* instanceProperties) {
    return vxr::VxrXrRuntime::GetInstance().GetInstanceProperties(instance, instanceProperties);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) {
    return vxr::VxrXrRuntime::GetInstance().GetSystem(instance, getInfo, systemId);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties* properties) {
    return vxr::VxrXrRuntime::GetInstance().GetSystemProperties(instance, systemId, properties);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance instance, XrSystemId systemId, uint32_t capacity, uint32_t* count, XrViewConfigurationType* types) {
    return vxr::VxrXrRuntime::GetInstance().EnumerateViewConfigurations(instance, systemId, capacity, count, types);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance instance, XrSystemId systemId, XrViewConfigurationType type, uint32_t capacity, uint32_t* count, XrViewConfigurationView* views) {
    return vxr::VxrXrRuntime::GetInstance().EnumerateViewConfigurationViews(instance, systemId, type, capacity, count, views);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId, XrViewConfigurationType type, uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* modes) {
    return vxr::VxrXrRuntime::GetInstance().EnumerateEnvironmentBlendModes(instance, systemId, type, capacity, count, modes);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
    return vxr::VxrXrRuntime::GetInstance().CreateSession(instance, createInfo, session);
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession session) {
    return vxr::VxrXrRuntime::GetInstance().DestroySession(session);
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo* beginInfo) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->BeginSession(beginInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession session) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->EndSession();
}

XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession session) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->RequestExitSession();
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->WaitFrame(frameWaitInfo, frameState);
}

XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->BeginFrame(frameBeginInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->LocateViews(viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->EndFrame(frameEndInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->CreateSwapchain(createInfo, swapchain);
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t imageCapacityInput, uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images) {
    auto sc = reinterpret_cast<vxr::VxrXrSwapchain*>(swapchain);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    return sc->EnumerateImages(imageCapacityInput, imageCountOutput, images);
}

XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index) {
    auto sc = reinterpret_cast<vxr::VxrXrSwapchain*>(swapchain);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    return sc->AcquireImage(acquireInfo, index);
}

XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo* waitInfo) {
    auto sc = reinterpret_cast<vxr::VxrXrSwapchain*>(swapchain);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    return sc->WaitImage(waitInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* releaseInfo) {
    auto sc = reinterpret_cast<vxr::VxrXrSwapchain*>(swapchain);
    if (!sc) return XR_ERROR_HANDLE_INVALID;
    return sc->ReleaseImage(releaseInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) {
    auto s = vxr::VxrXrRuntime::GetInstance().GetSession(session);
    if (!s) return XR_ERROR_HANDLE_INVALID;
    return s->CreateReferenceSpace(createInfo, space);
}

XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace space) {
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) {
    auto sp = reinterpret_cast<vxr::VxrXrSpace*>(space);
    auto bsp = reinterpret_cast<vxr::VxrXrSpace*>(baseSpace);
    return vxr::VxrXrSpace::LocateSpace(sp, bsp, time, location);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetD3D11GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D11KHR* requirements) {
    return vxr::VxrXrRuntime::GetInstance().GetD3D11GraphicsRequirementsKHR(instance, systemId, requirements);
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetD3D12GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D12KHR* requirements) {
    return vxr::VxrXrRuntime::GetInstance().GetD3D12GraphicsRequirementsKHR(instance, systemId, requirements);
}

// Khronos Loader Negotiation Entry Point
XrResult xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest
) {
    if (!loaderInfo || !runtimeRequest) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    runtimeRequest->structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    runtimeRequest->structVersion = XR_RUNTIME_REQUEST_STRUCT_VERSION;
    runtimeRequest->structSize = sizeof(XrNegotiateRuntimeRequest);
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;
    runtimeRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(xrGetInstanceProcAddr);

    return XR_SUCCESS;
}

// OpenXR Core Function Resolver
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function
) {
    if (!name || !function) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    if (strcmp(name, "xrNegotiateLoaderRuntimeInterface") == 0) {
        *function = reinterpret_cast<PFN_xrVoidFunction>(xrNegotiateLoaderRuntimeInterface);
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrGetInstanceProcAddr") == 0) {
        *function = reinterpret_cast<PFN_xrVoidFunction>(xrGetInstanceProcAddr);
        return XR_SUCCESS;
    }

    ROUTE_OPENXR_FUNC(xrCreateInstance);
    ROUTE_OPENXR_FUNC(xrDestroyInstance);
    ROUTE_OPENXR_FUNC(xrGetInstanceProperties);
    ROUTE_OPENXR_FUNC(xrGetSystem);
    ROUTE_OPENXR_FUNC(xrGetSystemProperties);
    ROUTE_OPENXR_FUNC(xrEnumerateViewConfigurations);
    ROUTE_OPENXR_FUNC(xrEnumerateViewConfigurationViews);
    ROUTE_OPENXR_FUNC(xrEnumerateEnvironmentBlendModes);
    ROUTE_OPENXR_FUNC(xrCreateSession);
    ROUTE_OPENXR_FUNC(xrDestroySession);
    ROUTE_OPENXR_FUNC(xrBeginSession);
    ROUTE_OPENXR_FUNC(xrEndSession);
    ROUTE_OPENXR_FUNC(xrRequestExitSession);
    ROUTE_OPENXR_FUNC(xrWaitFrame);
    ROUTE_OPENXR_FUNC(xrBeginFrame);
    ROUTE_OPENXR_FUNC(xrLocateViews);
    ROUTE_OPENXR_FUNC(xrEndFrame);
    ROUTE_OPENXR_FUNC(xrCreateSwapchain);
    ROUTE_OPENXR_FUNC(xrDestroySwapchain);
    ROUTE_OPENXR_FUNC(xrEnumerateSwapchainImages);
    ROUTE_OPENXR_FUNC(xrAcquireSwapchainImage);
    ROUTE_OPENXR_FUNC(xrWaitSwapchainImage);
    ROUTE_OPENXR_FUNC(xrReleaseSwapchainImage);
    ROUTE_OPENXR_FUNC(xrCreateReferenceSpace);
    ROUTE_OPENXR_FUNC(xrDestroySpace);
    ROUTE_OPENXR_FUNC(xrLocateSpace);
    ROUTE_OPENXR_FUNC(xrGetD3D11GraphicsRequirementsKHR);
    ROUTE_OPENXR_FUNC(xrGetD3D12GraphicsRequirementsKHR);

    *function = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // extern "C"
