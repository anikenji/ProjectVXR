#include "VxrIddAdapter.h"
#include <iostream>

namespace vxr {

VxrIddAdapter& VxrIddAdapter::GetInstance() {
    static VxrIddAdapter s_instance;
    return s_instance;
}

VxrIddAdapter::VxrIddAdapter()
    : m_initialized(true)
{
}

VxrIddAdapter::~VxrIddAdapter() {
    DestroyAllMonitors();
}

uint32_t VxrIddAdapter::CreateMonitor(uint32_t width, uint32_t height, uint32_t refresh_rate) {
    std::lock_guard<std::mutex> lock(m_adapterMutex);

    if (m_monitors.size() >= VXR_IDD_MAX_MONITORS) {
        std::cerr << "[VXR-IDDCX] Cannot create monitor: maximum of "
                  << VXR_IDD_MAX_MONITORS << " monitors reached!" << std::endl;
        return 0;
    }

    // Find first available monitor_id in [1, 2, 3]
    uint32_t free_id = 0;
    for (uint32_t id = 1; id <= VXR_IDD_MAX_MONITORS; ++id) {
        if (m_monitors.find(id) == m_monitors.end()) {
            free_id = id;
            break;
        }
    }

    if (free_id == 0) {
        return 0;
    }

    auto monitor = std::make_unique<VxrIddMonitor>(free_id, width, height, refresh_rate);
    m_monitors[free_id] = std::move(monitor);

    std::cout << "[VXR-IDDCX] Created Virtual Monitor #" << free_id
              << " (" << width << "x" << height << " @ " << refresh_rate << "Hz)"
              << " - Active monitors: " << m_monitors.size() << "/" << VXR_IDD_MAX_MONITORS << std::endl;

    return free_id;
}

bool VxrIddAdapter::DestroyMonitor(uint32_t monitor_id) {
    std::lock_guard<std::mutex> lock(m_adapterMutex);
    auto it = m_monitors.find(monitor_id);
    if (it == m_monitors.end()) {
        return false;
    }

    m_monitors.erase(it);
    std::cout << "[VXR-IDDCX] Destroyed Virtual Monitor #" << monitor_id
              << " - Active monitors: " << m_monitors.size() << std::endl;
    return true;
}

void VxrIddAdapter::DestroyAllMonitors() {
    std::lock_guard<std::mutex> lock(m_adapterMutex);
    m_monitors.clear();
}

VxrIddMonitor* VxrIddAdapter::GetMonitor(uint32_t monitor_id) {
    std::lock_guard<std::mutex> lock(m_adapterMutex);
    auto it = m_monitors.find(monitor_id);
    if (it == m_monitors.end()) {
        return nullptr;
    }
    return it->second.get();
}

uint32_t VxrIddAdapter::GetMonitorCount() const {
    std::lock_guard<std::mutex> lock(m_adapterMutex);
    return static_cast<uint32_t>(m_monitors.size());
}

uint32_t VxrIddAdapter::GetAllMonitorDescriptors(VxrMonitorDesc* out_descs, uint32_t max_count) const {
    if (!out_descs || max_count == 0) return 0;
    std::lock_guard<std::mutex> lock(m_adapterMutex);

    uint32_t copied = 0;
    for (const auto& pair : m_monitors) {
        if (copied >= max_count) break;
        pair.second->GetDescriptor(&out_descs[copied]);
        copied++;
    }
    return copied;
}

} // namespace vxr
