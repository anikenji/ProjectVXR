#ifndef VXR_XR_RUNTIME_H
#define VXR_XR_RUNTIME_H

#include "VxrXrSession.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace vxr {

class VxrXrRuntime {
public:
    static VxrXrRuntime& GetInstance();

    VxrXrRuntime();
    ~VxrXrRuntime();

    // Instance & System management
    XrResult CreateInstance(const XrInstanceCreateInfo* createInfo, XrInstance* instance);
    XrResult DestroyInstance(XrInstance instance);
    XrResult GetInstanceProperties(XrInstance instance, XrInstanceProperties* instanceProperties);
    XrResult GetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId);
    XrResult GetSystemProperties(XrInstance instance, XrSystemId systemId, XrSystemProperties* properties);
    XrResult EnumerateViewConfigurations(XrInstance instance, XrSystemId systemId, uint32_t capacity, uint32_t* count, XrViewConfigurationType* types);
    XrResult EnumerateViewConfigurationViews(XrInstance instance, XrSystemId systemId, XrViewConfigurationType type, uint32_t capacity, uint32_t* count, XrViewConfigurationView* views);
    XrResult EnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId, XrViewConfigurationType type, uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* modes);

    // Session management
    XrResult CreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session);
    XrResult DestroySession(XrSession session);
    VxrXrSession* GetSession(XrSession session);

    // D3D11 & D3D12 Graphics Requirements
    XrResult GetD3D11GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D11KHR* requirements);
    XrResult GetD3D12GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId, XrGraphicsRequirementsD3D12KHR* requirements);

    PFN_xrVoidFunction ResolveFunction(const char* name);

private:
    bool m_initialized;
    mutable std::mutex m_runtimeMutex;
    std::map<XrInstance, bool> m_instances;
    std::map<XrSession, std::unique_ptr<VxrXrSession>> m_sessions;
};

} // namespace vxr

#endif // VXR_XR_RUNTIME_H
