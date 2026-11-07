#ifndef VXR_IDD_ADAPTER_H
#define VXR_IDD_ADAPTER_H

#include "VxrIddMonitor.h"
#include <memory>
#include <map>
#include <mutex>

namespace vxr {

/// Indirect Display Adapter managing up to 3 concurrent virtual headless monitors
class VxrIddAdapter {
public:
    static VxrIddAdapter& GetInstance();

    VxrIddAdapter();
    ~VxrIddAdapter();

    /// Spawns a new virtual monitor (1 to 3 concurrent). Returns monitor_id, or 0 if limit reached.
    uint32_t CreateMonitor(uint32_t width, uint32_t height, uint32_t refresh_rate);

    /// Destroys a virtual monitor by monitor_id
    bool DestroyMonitor(uint32_t monitor_id);

    /// Destroys all currently active virtual monitors
    void DestroyAllMonitors();

    /// Retrieves monitor by monitor_id (1, 2, or 3)
    VxrIddMonitor* GetMonitor(uint32_t monitor_id);

    /// Total count of currently active virtual monitors
    uint32_t GetMonitorCount() const;

    /// Enumerates descriptors for all active monitors
    uint32_t GetAllMonitorDescriptors(VxrMonitorDesc* out_descs, uint32_t max_count) const;

    bool IsInitialized() const { return m_initialized; }

private:
    bool m_initialized;
    mutable std::mutex m_adapterMutex;
    std::map<uint32_t, std::unique_ptr<VxrIddMonitor>> m_monitors;
};

} // namespace vxr

#endif // VXR_IDD_ADAPTER_H
