#ifndef VXR_DRIVER_SERVER_PROVIDER_H
#define VXR_DRIVER_SERVER_PROVIDER_H

#include "openvr_driver.h"
#include "VxrHmdDevice.h"
#include "VxrControllerDevice.h"
#include <memory>
#include <atomic>

namespace vxr {

class VxrDriverServerProvider : public vr::IServerTrackedDeviceProvider {
public:
    static VxrDriverServerProvider& GetInstance();

    VxrDriverServerProvider();
    virtual ~VxrDriverServerProvider();

    // =========================================================================
    // IServerTrackedDeviceProvider interface
    // =========================================================================
    virtual vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    virtual void Cleanup() override;
    virtual const char* const* GetInterfaceVersions() override;
    virtual void RunFrame() override;
    virtual bool ShouldBlockStandbyMode() override;
    virtual void EnterStandby() override;
    virtual void LeaveStandby() override;

    // =========================================================================
    // Device Accessors
    // =========================================================================
    VxrHmdDevice* GetHmd() const { return m_pHmd.get(); }
    VxrControllerDevice* GetLeftController() const { return m_pLeftController.get(); }
    VxrControllerDevice* GetRightController() const { return m_pRightController.get(); }
    bool IsInitialized() const { return m_initialized.load(); }

private:
    std::atomic<bool> m_initialized;
    std::unique_ptr<VxrHmdDevice> m_pHmd;
    std::unique_ptr<VxrControllerDevice> m_pLeftController;
    std::unique_ptr<VxrControllerDevice> m_pRightController;
};

} // namespace vxr

#endif // VXR_DRIVER_SERVER_PROVIDER_H
