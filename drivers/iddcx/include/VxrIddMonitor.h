#ifndef VXR_IDD_MONITOR_H
#define VXR_IDD_MONITOR_H

#include "VxrIddTypes.h"
#include <string>
#include <vector>
#include <mutex>

namespace vxr {

class VxrIddMonitor {
public:
    VxrIddMonitor(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate);
    ~VxrIddMonitor();

    // Mode configuration
    bool SetMode(uint32_t width, uint32_t height, uint32_t refresh_rate);
    bool GetDescriptor(VxrMonitorDesc* out_desc) const;
    const uint8_t* GetEdid() const { return m_edid; }

    // Swapchain buffer lifecycle
    bool AssignSwapChain(uint32_t width, uint32_t height);
    void UnassignSwapChain();
    bool AcquireNextFrame(uint32_t timeout_ms, VxrIddFrame* out_frame);
    bool ReleaseFrame();

    uint32_t GetMonitorId() const { return m_monitorId; }
    uint32_t GetWidth() const { return m_currentWidth; }
    uint32_t GetHeight() const { return m_currentHeight; }
    uint32_t GetRefreshRate() const { return m_currentRefreshRate; }
    bool IsConnected() const { return m_isConnected; }
    bool HasActiveSwapChain() const { return m_hasSwapChain; }
    uint64_t GetFrameCount() const { return m_frameCount; }

private:
    void PopulateSupportedModes();

    uint32_t m_monitorId;
    std::string m_name;
    uint32_t m_currentWidth;
    uint32_t m_currentHeight;
    uint32_t m_currentRefreshRate;
    bool m_isConnected;
    bool m_hasSwapChain;
    bool m_frameAcquired;
    uint64_t m_frameCount;

    uint8_t m_edid[VXR_IDD_EDID_BLOCK_SIZE];
    std::vector<VxrDisplayMode> m_supportedModes;

    mutable std::recursive_mutex m_mutex;
    void* m_d3d11Texture;
    void* m_sharedHandle;
};

} // namespace vxr

#endif // VXR_IDD_MONITOR_H
