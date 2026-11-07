#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VxrXrNegotiation.h"
#include "VxrXrRuntime.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: VXR-XR Native Runtime Test Suite " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: Khronos Loader-Runtime Negotiation
    std::cout << "[Test 1] Testing Loader-Runtime Interface Negotiation..." << std::endl;
    {
        XrNegotiateLoaderInfo loaderInfo;
        memset(&loaderInfo, 0, sizeof(loaderInfo));
        loaderInfo.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
        loaderInfo.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
        loaderInfo.structSize = sizeof(XrNegotiateLoaderInfo);
        loaderInfo.minInterfaceVersion = 1;
        loaderInfo.maxInterfaceVersion = 1;
        loaderInfo.minApiVersion = XR_CURRENT_API_VERSION;
        loaderInfo.maxApiVersion = XR_CURRENT_API_VERSION;

        XrNegotiateRuntimeRequest runtimeRequest;
        memset(&runtimeRequest, 0, sizeof(runtimeRequest));

        XrResult res = xrNegotiateLoaderRuntimeInterface(&loaderInfo, &runtimeRequest);
        assert(res == XR_SUCCESS);
        assert(runtimeRequest.structType == XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST);
        assert(runtimeRequest.runtimeInterfaceVersion == 1);
        assert(runtimeRequest.getInstanceProcAddr != nullptr);
        std::cout << "  -> Negotiation succeeded with Khronos loader interface v1." << std::endl;

        // Verify invalid negotiation rejection
        loaderInfo.structType = XR_LOADER_INTERFACE_STRUCT_UNINTIALIZED;
        assert(xrNegotiateLoaderRuntimeInterface(&loaderInfo, &runtimeRequest) == XR_ERROR_INITIALIZATION_FAILED);
    }

    // Test 2: Function Pointer Resolution via xrGetInstanceProcAddr
    std::cout << "[Test 2] Testing API function resolution via xrGetInstanceProcAddr..." << std::endl;
    {
        PFN_xrVoidFunction pfn = nullptr;
        assert(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", &pfn) == XR_SUCCESS);
        assert(pfn != nullptr);

        assert(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateSession", &pfn) == XR_SUCCESS);
        assert(pfn != nullptr);

        assert(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrWaitFrame", &pfn) == XR_SUCCESS);
        assert(pfn != nullptr);

        assert(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrGetD3D11GraphicsRequirementsKHR", &pfn) == XR_SUCCESS);
        assert(pfn != nullptr);

        assert(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrUnsupportedFunction", &pfn) == XR_ERROR_FUNCTION_UNSUPPORTED);
        assert(pfn == nullptr);
        std::cout << "  -> Function resolution verified for core OpenXR 1.0/1.1 APIs." << std::endl;
    }

    // Test 3: Instance & System Properties (Quest 3 Emulation)
    std::cout << "[Test 3] Testing Instance & System properties..." << std::endl;
    XrInstance instance = XR_NULL_HANDLE;
    {
        XrInstanceCreateInfo createInfo;
        memset(&createInfo, 0, sizeof(createInfo));
        createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
        strncpy(createInfo.applicationInfo.applicationName, "VXR Sim Test", XR_MAX_APPLICATION_NAME_SIZE - 1);
        createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

        assert(xrCreateInstance(&createInfo, &instance) == XR_SUCCESS);
        assert(instance != XR_NULL_HANDLE);

        XrInstanceProperties instProps;
        memset(&instProps, 0, sizeof(instProps));
        instProps.type = XR_TYPE_INSTANCE_PROPERTIES;
        assert(xrGetInstanceProperties(instance, &instProps) == XR_SUCCESS);
        assert(strcmp(instProps.runtimeName, "Project VXR-XR Native Runtime") == 0);
        std::cout << "  -> Runtime Name: " << instProps.runtimeName << std::endl;

        // Query System
        XrSystemGetInfo sysInfo;
        memset(&sysInfo, 0, sizeof(sysInfo));
        sysInfo.type = XR_TYPE_SYSTEM_GET_INFO;
        sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        XrSystemId systemId = 0;
        assert(xrGetSystem(instance, &sysInfo, &systemId) == XR_SUCCESS);
        assert(systemId == 1);

        XrSystemProperties sysProps;
        memset(&sysProps, 0, sizeof(sysProps));
        sysProps.type = XR_TYPE_SYSTEM_PROPERTIES;
        assert(xrGetSystemProperties(instance, systemId, &sysProps) == XR_SUCCESS);
        assert(strcmp(sysProps.systemName, "Meta Quest 3 (VXR-XR)") == 0);
        assert(sysProps.trackingProperties.orientationTracking == XR_TRUE);
        assert(sysProps.trackingProperties.positionTracking == XR_TRUE);
        std::cout << "  -> System Name: " << sysProps.systemName << " (6DoF Tracking=YES)" << std::endl;

        // Enumerate Views
        uint32_t viewCount = 0;
        assert(xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr) == XR_SUCCESS);
        assert(viewCount == 2);

        XrViewConfigurationView views[2];
        memset(views, 0, sizeof(views));
        views[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        assert(xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, views) == XR_SUCCESS);

        assert(views[0].recommendedImageRectWidth == 2064);
        assert(views[0].recommendedImageRectHeight == 2208);
        assert(views[1].recommendedImageRectWidth == 2064);
        assert(views[1].recommendedImageRectHeight == 2208);
        std::cout << "  -> Native Eye Resolution: " << views[0].recommendedImageRectWidth
                  << "x" << views[0].recommendedImageRectHeight << " per eye" << std::endl;
    }

    // Test 4: Session Creation & D3D11 Graphics Binding
    std::cout << "[Test 4] Testing Session creation & D3D11 graphics requirements..." << std::endl;
    XrSession session = XR_NULL_HANDLE;
    {
        PFN_xrGetD3D11GraphicsRequirementsKHR pfnGetD3D11 = nullptr;
        assert(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetD3D11)) == XR_SUCCESS);
        assert(pfnGetD3D11 != nullptr);

        XrGraphicsRequirementsD3D11KHR d3d11Reqs;
        memset(&d3d11Reqs, 0, sizeof(d3d11Reqs));
        d3d11Reqs.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
        assert(pfnGetD3D11(instance, 1, &d3d11Reqs) == XR_SUCCESS);
        assert(d3d11Reqs.minFeatureLevel == D3D_FEATURE_LEVEL_11_0);

        XrGraphicsBindingD3D11KHR d3d11Binding;
        memset(&d3d11Binding, 0, sizeof(d3d11Binding));
        d3d11Binding.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
        d3d11Binding.device = reinterpret_cast<ID3D11Device*>(0x12345678ULL);

        XrSessionCreateInfo sessionInfo;
        memset(&sessionInfo, 0, sizeof(sessionInfo));
        sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
        sessionInfo.next = &d3d11Binding;
        sessionInfo.systemId = 1;

        assert(xrCreateSession(instance, &sessionInfo, &session) == XR_SUCCESS);
        assert(session != XR_NULL_HANDLE);

        XrSessionBeginInfo beginInfo;
        memset(&beginInfo, 0, sizeof(beginInfo));
        beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        assert(xrBeginSession(session, &beginInfo) == XR_SUCCESS);
        std::cout << "  -> OpenXR session created and begun with D3D11 binding." << std::endl;
    }

    // Test 5: Swapchain Creation & Shared Handle Extraction
    std::cout << "[Test 5] Testing Swapchain creation and zero-copy shared handle extraction..." << std::endl;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    {
        XrSwapchainCreateInfo scInfo;
        memset(&scInfo, 0, sizeof(scInfo));
        scInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        scInfo.format = 29; // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        scInfo.sampleCount = 1;
        scInfo.width = 2064;
        scInfo.height = 2208;
        scInfo.faceCount = 1;
        scInfo.arraySize = 1;
        scInfo.mipCount = 1;

        assert(xrCreateSwapchain(session, &scInfo, &swapchain) == XR_SUCCESS);
        assert(swapchain != XR_NULL_HANDLE);

        uint32_t imageCount = 0;
        assert(xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr) == XR_SUCCESS);
        assert(imageCount == 3);

        XrSwapchainImageD3D11KHR scImages[3];
        memset(scImages, 0, sizeof(scImages));
        assert(xrEnumerateSwapchainImages(swapchain, 3, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(scImages)) == XR_SUCCESS);
        assert(scImages[0].texture != nullptr);

        uint32_t acquiredIdx = 999;
        XrSwapchainImageAcquireInfo acqInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, nullptr };
        assert(xrAcquireSwapchainImage(swapchain, &acqInfo, &acquiredIdx) == XR_SUCCESS);
        assert(acquiredIdx == 0);

        auto sc = reinterpret_cast<vxr::VxrXrSwapchain*>(swapchain);
        assert(sc->GetSharedHandle(0) != nullptr);
        std::cout << "  -> Triple-buffered swapchain created: shared_handle="
                  << sc->GetSharedHandle(0) << std::endl;

        XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, 1000000 };
        assert(xrWaitSwapchainImage(swapchain, &waitInfo) == XR_SUCCESS);

        XrSwapchainImageReleaseInfo relInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, nullptr };
        assert(xrReleaseSwapchainImage(swapchain, &relInfo) == XR_SUCCESS);
    }

    // Test 6: Frame Loop & View Projection
    std::cout << "[Test 6] Testing OpenXR Frame Loop (WaitFrame, BeginFrame, LocateViews, EndFrame)..." << std::endl;
    {
        XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO, nullptr };
        XrFrameState frameState;
        memset(&frameState, 0, sizeof(frameState));
        frameState.type = XR_TYPE_FRAME_STATE;
        assert(xrWaitFrame(session, &waitInfo, &frameState) == XR_SUCCESS);
        assert(frameState.shouldRender == XR_TRUE);
        assert(frameState.predictedDisplayPeriod == 11111111ULL); // 90Hz
        std::cout << "  -> WaitFrame: Predicted Display Time=" << frameState.predictedDisplayTime
                  << " ns (Period=11.1ms)" << std::endl;

        XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO, nullptr };
        assert(xrBeginFrame(session, &beginInfo) == XR_SUCCESS);

        // Locate views
        XrViewLocateInfo viewLocInfo;
        memset(&viewLocInfo, 0, sizeof(viewLocInfo));
        viewLocInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
        viewLocInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocInfo.displayTime = frameState.predictedDisplayTime;

        XrViewState viewState;
        memset(&viewState, 0, sizeof(viewState));
        viewState.type = XR_TYPE_VIEW_STATE;

        uint32_t viewCount = 0;
        XrView views[2];
        memset(views, 0, sizeof(views));
        assert(xrLocateViews(session, &viewLocInfo, &viewState, 2, &viewCount, views) == XR_SUCCESS);
        assert(viewCount == 2);
        // Verify 63mm IPD (-31.5mm left, +31.5mm right)
        assert(std::abs(views[0].pose.position.x - (-0.0315f)) < 1e-4);
        assert(std::abs(views[1].pose.position.x - 0.0315f) < 1e-4);
        std::cout << "  -> LocateViews: IPD=" << (views[1].pose.position.x - views[0].pose.position.x) * 1000.0f
                  << " mm, Tangent FOV=(" << views[0].fov.angleLeft << ", " << views[0].fov.angleRight << ")" << std::endl;

        // End frame with projection layer
        XrCompositionLayerProjectionView projViews[2];
        memset(projViews, 0, sizeof(projViews));
        projViews[0].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projViews[0].pose = views[0].pose;
        projViews[0].fov = views[0].fov;
        projViews[0].subImage.swapchain = swapchain;
        projViews[0].subImage.imageRect.offset.x = 0;
        projViews[0].subImage.imageRect.offset.y = 0;
        projViews[0].subImage.imageRect.extent.width = 2064;
        projViews[0].subImage.imageRect.extent.height = 2208;
        projViews[0].subImage.imageArrayIndex = 0;

        XrCompositionLayerProjection layer;
        memset(&layer, 0, sizeof(layer));
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.viewCount = 1;
        layer.views = projViews;

        const XrCompositionLayerBaseHeader* layers[1] = { reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer) };

        XrFrameEndInfo endInfo;
        memset(&endInfo, 0, sizeof(endInfo));
        endInfo.type = XR_TYPE_FRAME_END_INFO;
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = 1;
        endInfo.layers = layers;

        assert(xrEndFrame(session, &endInfo) == XR_SUCCESS);
        auto sessPtr = vxr::VxrXrRuntime::GetInstance().GetSession(session);
        assert(sessPtr->GetFrameCount() == 1);
        assert(sessPtr->GetLastSubmittedSharedHandle() != nullptr);
        std::cout << "  -> EndFrame: Successfully intercepted rendered frame shared handle for zero-copy streaming!" << std::endl;
    }

    // Cleanup
    assert(xrDestroySwapchain(swapchain) == XR_SUCCESS);
    assert(xrEndSession(session) == XR_SUCCESS);
    assert(xrDestroySession(session) == XR_SUCCESS);
    assert(xrDestroyInstance(instance) == XR_SUCCESS);
    std::cout << "  -> Clean session and instance destruction verified." << std::endl;

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL VXR-XR RUNTIME TESTS PASSED!              " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
