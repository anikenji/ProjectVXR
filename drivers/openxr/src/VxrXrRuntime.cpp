#include "VxrXrRuntime.h"
#include <cstring>
#include <iostream>

namespace vxr {

VxrXrRuntime& VxrXrRuntime::GetInstance() {
    static VxrXrRuntime s_instance;
    return s_instance;
}

VxrXrRuntime::VxrXrRuntime()
    : m_initialized(true)
{
}

VxrXrRuntime::~VxrXrRuntime() {
}

XrResult VxrXrRuntime::CreateInstance(const XrInstanceCreateInfo* createInfo, XrInstance* instance) {
    if (!createInfo || !instance) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_runtimeMutex);

    XrInstance handle = reinterpret_cast<XrInstance>(0x10001000ULL);
    m_instances[handle] = true;
    *instance = handle;
    return XR_SUCCESS;
}

XrResult VxrXrRuntime::DestroyInstance(XrInstance instance) {
    std::lock_guard<std::mutex> lock(m_runtimeMutex);
    m_instances.erase(instance);
    return XR_SUCCESS;
}

XrResult VxrXrRuntime::GetInstanceProperties(XrInstance instance, XrInstanceProperties* instanceProperties) {
    if (!instanceProperties) return XR_ERROR_VALIDATION_FAILURE;

    instanceProperties->type = XR_TYPE_INSTANCE_PROPERTIES;
    instanceProperties->next = nullptr;
    strncpy(instanceProperties->runtimeName, "Project VXR-XR Native Runtime", XR_MAX_RUNTIME_NAME_SIZE - 1);
    instanceProperties->runtimeName[XR_MAX_RUNTIME_NAME_SIZE - 1] = 0;
    instanceProperties->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);

    return XR_SUCCESS;
}

XrResult VxrXrRuntime::GetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) {
    if (!getInfo || !systemId) return XR_ERROR_VALIDATION_FAILURE;
    if (getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
        return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    }

    *systemId = 1;
    return XR_SUCCESS;
}

XrResult VxrXrRuntime::GetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties* properties) {
    if (!properties || systemId != 1) return XR_ERROR_VALIDATION_FAILURE;

    properties->type = XR_TYPE_SYSTEM_PROPERTIES;
    properties->next = nullptr;
    properties->systemId = systemId;
    properties->vendorId = 0x5A8E;
    strncpy(properties->systemName, "Meta Quest 3 (VXR-XR)", XR_MAX_SYSTEM_NAME_SIZE - 1);
    properties->systemName[XR_MAX_SYSTEM_NAME_SIZE - 1] = 0;

    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    properties->graphicsProperties.maxLayerCount = 16;

    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;

    return XR_SUCCESS;
}

XrResult VxrXrRuntime::EnumerateViewConfigurations(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t capacity,
    uint32_t* count,
    XrViewConfigurationType* types
) {
    if (!count) return XR_ERROR_VALIDATION_FAILURE;
    *count = 1;

    if (capacity == 0) {
        return XR_SUCCESS;
    }

    if (types) {
        types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    return XR_SUCCESS;
}

XrResult VxrXrRuntime::EnumerateViewConfigurationViews(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType type,
    uint32_t capacity,
    uint32_t* count,
    XrViewConfigurationView* views
) {
    if (!count) return XR_ERROR_VALIDATION_FAILURE;
    *count = 2; // Stereo (Left and Right)

    if (capacity == 0) {
        return XR_SUCCESS;
    }

    if (capacity < 2) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }

    uint32_t scaledW = static_cast<uint32_t>(2064.0f * m_renderScale);
    uint32_t scaledH = static_cast<uint32_t>(2208.0f * m_renderScale);
    if (scaledW < 512) scaledW = 512;
    if (scaledH < 512) scaledH = 512;

    // View 0: Left Eye (scaled by VXR-XR Render Resolution)
    views[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    views[0].next = nullptr;
    views[0].recommendedImageRectWidth = scaledW;
    views[0].maxImageRectWidth = 4096;
    views[0].recommendedImageRectHeight = scaledH;
    views[0].maxImageRectHeight = 4096;
    views[0].recommendedSwapchainSampleCount = 1;
    views[0].maxSwapchainSampleCount = 4;

    // View 1: Right Eye (scaled by VXR-XR Render Resolution)
    views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    views[1].next = nullptr;
    views[1].recommendedImageRectWidth = scaledW;
    views[1].maxImageRectWidth = 4096;
    views[1].recommendedImageRectHeight = scaledH;
    views[1].maxImageRectHeight = 4096;
    views[1].recommendedSwapchainSampleCount = 1;
    views[1].maxSwapchainSampleCount = 4;

    return XR_SUCCESS;
}

void VxrXrRuntime::SetOpticalConfig(float hFovScale, float vFovScale, float renderScale, float foveaRatio, bool useFovStencil) {
    std::lock_guard<std::mutex> lock(m_runtimeMutex);
    m_hFovScale = hFovScale;
    m_vFovScale = vFovScale;
    m_renderScale = renderScale;
    m_foveaRatio = foveaRatio;
    m_useFovStencil = useFovStencil;
}

XrResult VxrXrRuntime::EnumerateEnvironmentBlendModes(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType type,
    uint32_t capacity,
    uint32_t* count,
    XrEnvironmentBlendMode* modes
) {
    if (!count) return XR_ERROR_VALIDATION_FAILURE;
    *count = 2; // Opaque and Additive (Passthrough)

    if (capacity == 0) return XR_SUCCESS;

    if (modes) {
        modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        if (capacity > 1) {
            modes[1] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
        }
    }
    return XR_SUCCESS;
}

XrResult VxrXrRuntime::CreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
    if (!createInfo || !session) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_runtimeMutex);

    auto sess = std::make_unique<VxrXrSession>(instance, createInfo->next);
    XrSession handle = reinterpret_cast<XrSession>(sess.get());
    m_sessions[handle] = std::move(sess);
    *session = handle;

    return XR_SUCCESS;
}

XrResult VxrXrRuntime::DestroySession(XrSession session) {
    std::lock_guard<std::mutex> lock(m_runtimeMutex);
    m_sessions.erase(session);
    return XR_SUCCESS;
}

VxrXrSession* VxrXrRuntime::GetSession(XrSession session) {
    std::lock_guard<std::mutex> lock(m_runtimeMutex);
    auto it = m_sessions.find(session);
    if (it != m_sessions.end()) {
        return it->second.get();
    }
    return nullptr;
}

XrResult VxrXrRuntime::GetD3D11GraphicsRequirementsKHR(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsD3D11KHR* requirements
) {
    if (!requirements) return XR_ERROR_VALIDATION_FAILURE;

    requirements->type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
    requirements->next = nullptr;
    requirements->adapterLuid = LUID{ 0, 0 };
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    return XR_SUCCESS;
}

XrResult VxrXrRuntime::GetD3D12GraphicsRequirementsKHR(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsD3D12KHR* requirements
) {
    if (!requirements) return XR_ERROR_VALIDATION_FAILURE;

    requirements->type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR;
    requirements->next = nullptr;
    requirements->adapterLuid = LUID{ 0, 0 };
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    return XR_SUCCESS;
}

} // namespace vxr
