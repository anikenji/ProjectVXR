#ifndef VXR_XR_SESSION_H
#define VXR_XR_SESSION_H

#include "VxrXrSwapchain.h"
#include "VxrXrSpaces.h"
#include <map>
#include <memory>
#include <mutex>

namespace vxr {

enum class XrGraphicsBindingType {
    D3D11,
    D3D12,
    Vulkan,
    Headless,
};

class VxrXrSession {
public:
    VxrXrSession(XrInstance instance, const void* graphicsBinding);
    ~VxrXrSession();

    XrResult BeginSession(const XrSessionBeginInfo* beginInfo);
    XrResult EndSession();
    XrResult RequestExitSession();

    // Frame timing loop
    XrResult WaitFrame(const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState);
    XrResult BeginFrame(const XrFrameBeginInfo* frameBeginInfo);
    XrResult LocateViews(const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState, uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views);
    XrResult EndFrame(const XrFrameEndInfo* frameEndInfo);

    // Swapchains
    XrResult CreateSwapchain(const XrSwapchainCreateInfo* createInfo, XrSwapchain* swapchain);
    XrResult DestroySwapchain(XrSwapchain swapchain);

    // Spaces
    XrResult CreateReferenceSpace(const XrReferenceSpaceCreateInfo* createInfo, XrSpace* space);
    XrResult DestroySpace(XrSpace space);

    XrSessionState GetState() const { return m_state; }
    bool IsRunning() const { return m_isRunning; }
    uint64_t GetFrameCount() const { return m_frameCount; }
    HANDLE GetLastSubmittedSharedHandle() const { return m_lastSubmittedSharedHandle; }

private:
    XrInstance m_instance;
    XrGraphicsBindingType m_bindingType;
    XrSessionState m_state;
    bool m_isRunning;
    uint64_t m_frameCount;
    HANDLE m_lastSubmittedSharedHandle;

    mutable std::mutex m_sessionMutex;
    std::map<XrSwapchain, std::unique_ptr<VxrXrSwapchain>> m_swapchains;
    std::map<XrSpace, std::unique_ptr<VxrXrSpace>> m_spaces;
};

} // namespace vxr

#endif // VXR_XR_SESSION_H
