#ifndef VXR_WIFI_OPTIMIZER_H
#define VXR_WIFI_OPTIMIZER_H

#include <windows.h>
#include <wlanapi.h>
#include <vector>
#include <atomic>

namespace vxr {

/// Disables Windows periodic 60-second Wi-Fi background scanning during active VR streaming
/// to eliminate 100ms-300ms ping spikes and frame stutter (Virtual Desktop Phase A.1 inheritance).
class VxrWifiOptimizer {
public:
    VxrWifiOptimizer();
    ~VxrWifiOptimizer();

    VxrWifiOptimizer(const VxrWifiOptimizer&) = delete;
    VxrWifiOptimizer& operator=(const VxrWifiOptimizer&) = delete;

    /// Opens a WLAN client handle and disables background scanning on all active wireless interfaces.
    bool LockBackgroundScan();

    /// Restores background scanning on all locked wireless interfaces and closes the WLAN handle.
    void UnlockBackgroundScan();

    /// Returns true if Wi-Fi background scanning is currently locked (disabled).
    bool IsScanLocked() const { return m_scanLocked.load(); }

    /// Returns number of wireless interfaces whose background scan was locked.
    uint32_t GetLockedInterfaceCount() const { return m_lockedCount; }

private:
    HANDLE m_wlanClient;
    std::vector<GUID> m_lockedGuids;
    std::atomic<bool> m_scanLocked;
    uint32_t m_lockedCount;
};

} // namespace vxr

#endif // VXR_WIFI_OPTIMIZER_H
