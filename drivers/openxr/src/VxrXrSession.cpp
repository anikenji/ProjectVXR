#include "VxrXrSession.h"
#include "VxrXrRuntime.h"
#include <chrono>
#include <cmath>
#include <iostream>

namespace vxr {

VxrXrSession::VxrXrSession(XrInstance instance, const void* graphicsBinding)
    : m_instance(instance)
    , m_bindingType(XrGraphicsBindingType::D3D11)
    , m_state(XrSessionState::XR_SESSION_STATE_IDLE)
    , m_isRunning(false)
    , m_frameCount(0)
    , m_lastSubmittedSharedHandle(nullptr)
{
    if (graphicsBinding != nullptr) {
        auto header = reinterpret_cast<const XrBaseInStructure*>(graphicsBinding);
        if (header->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            m_bindingType = XrGraphicsBindingType::D3D11;
        } else if (header->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            m_bindingType = XrGraphicsBindingType::D3D12;
        }
    }
}

VxrXrSession::~VxrXrSession() {
    EndSession();
}

XrResult VxrXrSession::BeginSession(const XrSessionBeginInfo* beginInfo) {
    if (!beginInfo) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_sessionMutex);

    m_isRunning = true;
    m_state = XR_SESSION_STATE_FOCUSED;
    return XR_SUCCESS;
}

XrResult VxrXrSession::EndSession() {
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    m_isRunning = false;
    m_state = XR_SESSION_STATE_IDLE;
    m_swapchains.clear();
    m_spaces.clear();
    return XR_SUCCESS;
}

XrResult VxrXrSession::RequestExitSession() {
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    m_state = XR_SESSION_STATE_STOPPING;
    return XR_SUCCESS;
}

XrResult VxrXrSession::WaitFrame(const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
    if (!frameState) return XR_ERROR_VALIDATION_FAILURE;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

    // 90Hz frame period: 11.1ms (11,111,111 ns)
    const uint64_t framePeriodNs = 11111111ULL;

    frameState->type = XR_TYPE_FRAME_STATE;
    frameState->next = nullptr;
    frameState->predictedDisplayTime = now_ns + framePeriodNs;
    frameState->predictedDisplayPeriod = framePeriodNs;
    frameState->shouldRender = m_isRunning ? XR_TRUE : XR_FALSE;

    return XR_SUCCESS;
}

XrResult VxrXrSession::BeginFrame(const XrFrameBeginInfo* frameBeginInfo) {
    return XR_SUCCESS;
}

XrResult VxrXrSession::LocateViews(
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views
) {
    if (!viewCountOutput || !viewState) return XR_ERROR_VALIDATION_FAILURE;

    *viewCountOutput = 2; // Dual eye (Left and Right)
    if (viewCapacityInput == 0) {
        return XR_SUCCESS;
    }

    if (viewCapacityInput < 2) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }

    viewState->type = XR_TYPE_VIEW_STATE;
    viewState->next = nullptr;
    viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT
                              | XR_VIEW_STATE_POSITION_VALID_BIT
                              | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT
                              | XR_VIEW_STATE_POSITION_TRACKED_BIT;

    float hScale = VxrXrRuntime::GetInstance().GetHorizontalFovScale();
    float vScale = VxrXrRuntime::GetInstance().GetVerticalFovScale();
    // Apply Horizontal & Vertical FOV Tangent clamping (default 1.05 rad when scale = 1.0)
    float angleH = (hScale == 1.0f) ? 1.05f : std::atan(std::tan(1.05f) * hScale);
    float angleV = (vScale == 1.0f) ? 1.05f : std::atan(std::tan(1.05f) * vScale);

    // Left eye view: IPD = -0.0315m
    views[0].type = XR_TYPE_VIEW;
    views[0].next = nullptr;
    views[0].pose.position.x = -0.0315f;
    views[0].pose.position.y = 1.70f;
    views[0].pose.position.z = 0.0f;
    views[0].pose.orientation.x = 0.0f;
    views[0].pose.orientation.y = 0.0f;
    views[0].pose.orientation.z = 0.0f;
    views[0].pose.orientation.w = 1.0f;
    views[0].fov.angleLeft = -angleH;
    views[0].fov.angleRight = angleH;
    views[0].fov.angleUp = angleV;
    views[0].fov.angleDown = -angleV;

    // Right eye view: IPD = +0.0315m
    views[1].type = XR_TYPE_VIEW;
    views[1].next = nullptr;
    views[1].pose.position.x = 0.0315f;
    views[1].pose.position.y = 1.70f;
    views[1].pose.position.z = 0.0f;
    views[1].pose.orientation.x = 0.0f;
    views[1].pose.orientation.y = 0.0f;
    views[1].pose.orientation.z = 0.0f;
    views[1].pose.orientation.w = 1.0f;
    views[1].fov.angleLeft = -angleH;
    views[1].fov.angleRight = angleH;
    views[1].fov.angleUp = angleV;
    views[1].fov.angleDown = -angleV;

    return XR_SUCCESS;
}

XrResult VxrXrSession::EndFrame(const XrFrameEndInfo* frameEndInfo) {
    if (!frameEndInfo) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_sessionMutex);

    m_frameCount++;

    // Intercept submitted projection layers and grab shared handle
    for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
        const auto layerHeader = frameEndInfo->layers[i];
        if (layerHeader && layerHeader->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            auto projLayer = reinterpret_cast<const XrCompositionLayerProjection*>(layerHeader);
            if (projLayer->viewCount > 0) {
                XrSwapchain swapchainHandle = projLayer->views[0].subImage.swapchain;
                auto it = m_swapchains.find(swapchainHandle);
                if (it != m_swapchains.end()) {
                    uint32_t imgIdx = projLayer->views[0].subImage.imageArrayIndex;
                    m_lastSubmittedSharedHandle = it->second->GetSharedHandle(imgIdx);
                }
            }
        }
    }

    return XR_SUCCESS;
}

XrResult VxrXrSession::CreateSwapchain(const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain) {
    if (!createInfo || !swapchain) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_sessionMutex);

    auto sc = std::make_unique<VxrXrSwapchain>(*createInfo);
    XrSwapchain handle = reinterpret_cast<XrSwapchain>(sc.get());
    m_swapchains[handle] = std::move(sc);
    *swapchain = handle;

    return XR_SUCCESS;
}

XrResult VxrXrSession::DestroySwapchain(XrSwapchain swapchain) {
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    m_swapchains.erase(swapchain);
    return XR_SUCCESS;
}

XrResult VxrXrSession::CreateReferenceSpace(const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space) {
    if (!createInfo || !space) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_sessionMutex);

    auto sp = std::make_unique<VxrXrSpace>(createInfo->referenceSpaceType, createInfo->poseInReferenceSpace);
    XrSpace handle = reinterpret_cast<XrSpace>(sp.get());
    m_spaces[handle] = std::move(sp);
    *space = handle;

    return XR_SUCCESS;
}

XrResult VxrXrSession::DestroySpace(XrSpace space) {
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    m_spaces.erase(space);
    return XR_SUCCESS;
}

} // namespace vxr
