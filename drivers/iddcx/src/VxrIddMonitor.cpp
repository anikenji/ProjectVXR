#include "VxrIddMonitor.h"
#include "VxrEdidSynthesizer.h"
#include <cstring>
#include <chrono>

namespace vxr {

VxrIddMonitor::VxrIddMonitor(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate)
    : m_monitorId(monitor_id)
    , m_currentWidth(width)
    , m_currentHeight(height)
    , m_currentRefreshRate(refresh_rate)
    , m_isConnected(true)
    , m_hasSwapChain(false)
    , m_frameAcquired(false)
    , m_frameCount(0)
    , m_d3d11Texture(nullptr)
    , m_sharedHandle(nullptr)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "VXR Virtual Display #%u", monitor_id);
    m_name = buf;

    PopulateSupportedModes();
    VxrEdidSynthesizer::Synthesize(m_monitorId, m_currentWidth, m_currentHeight, m_currentRefreshRate, m_edid);
    AssignSwapChain(m_currentWidth, m_currentHeight);
}

VxrIddMonitor::~VxrIddMonitor() {
    UnassignSwapChain();
}

void VxrIddMonitor::PopulateSupportedModes() {
    m_supportedModes.clear();

    const uint32_t refreshRates[] = { 60, 72, 90, 120 };
    const struct { uint32_t w; uint32_t h; } resolutions[] = {
        { 1920, 1080 }, // 1080p FHD
        { 2560, 1440 }, // 1440p QHD
        { 3840, 2160 }  // 4K UHD
    };

    for (const auto& res : resolutions) {
        for (uint32_t fps : refreshRates) {
            VxrDisplayMode mode;
            mode.width = res.w;
            mode.height = res.h;
            mode.refresh_rate_num = fps;
            mode.refresh_rate_den = 1;
            m_supportedModes.push_back(mode);
        }
    }
}

bool VxrIddMonitor::SetMode(uint32_t width, uint32_t height, uint32_t refresh_rate) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Verify mode is supported up to 4K 120Hz
    bool valid = false;
    for (const auto& m : m_supportedModes) {
        if (m.width == width && m.height == height && m.refresh_rate_num == refresh_rate) {
            valid = true;
            break;
        }
    }

    if (!valid && width <= 3840 && height <= 2160 && refresh_rate <= 120) {
        valid = true; // allow custom within bounds
    }

    if (!valid) {
        return false;
    }

    m_currentWidth = width;
    m_currentHeight = height;
    m_currentRefreshRate = refresh_rate;

    VxrEdidSynthesizer::Synthesize(m_monitorId, m_currentWidth, m_currentHeight, m_currentRefreshRate, m_edid);

    // Recreate swapchain for new resolution
    if (m_hasSwapChain) {
        UnassignSwapChain();
        AssignSwapChain(m_currentWidth, m_currentHeight);
    }

    return true;
}

bool VxrIddMonitor::GetDescriptor(VxrMonitorDesc* out_desc) const {
    if (!out_desc) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    out_desc->monitor_id = m_monitorId;
    strncpy(out_desc->name, m_name.c_str(), sizeof(out_desc->name) - 1);
    out_desc->name[sizeof(out_desc->name) - 1] = 0;
    out_desc->current_width = m_currentWidth;
    out_desc->current_height = m_currentHeight;
    out_desc->current_refresh_rate = m_currentRefreshRate;
    out_desc->is_connected = m_isConnected;

    uint32_t count = static_cast<uint32_t>(std::min(m_supportedModes.size(), size_t(16)));
    out_desc->supported_modes_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        out_desc->supported_modes[i] = m_supportedModes[i];
    }

    return true;
}

bool VxrIddMonitor::AssignSwapChain(uint32_t width, uint32_t height) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    m_d3d11Texture = reinterpret_cast<void*>(static_cast<uintptr_t>(0xD3D11000ULL + m_monitorId * 0x1000ULL));
    m_sharedHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(0xF0000000ULL + m_monitorId * 0x1000ULL));
    m_hasSwapChain = true;
    m_frameAcquired = false;
    return true;
}

void VxrIddMonitor::UnassignSwapChain() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_hasSwapChain = false;
    m_frameAcquired = false;
    m_d3d11Texture = nullptr;
    m_sharedHandle = nullptr;
}

bool VxrIddMonitor::AcquireNextFrame(uint32_t timeout_ms, VxrIddFrame* out_frame) {
    if (!out_frame) return false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (!m_hasSwapChain) {
        return false;
    }

    if (m_frameAcquired) {
        m_frameAcquired = false;
    }

    m_frameCount++;
    m_frameAcquired = true;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t qpc = std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    out_frame->d3d11_texture = m_d3d11Texture;
    out_frame->shared_handle = m_sharedHandle;
    out_frame->qpc_timestamp = qpc;
    out_frame->frame_index = m_frameCount;
    out_frame->width = m_currentWidth;
    out_frame->height = m_currentHeight;
    out_frame->has_new_frame = true;

    return true;
}

bool VxrIddMonitor::ReleaseFrame() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_hasSwapChain || !m_frameAcquired) {
        return false;
    }
    m_frameAcquired = false;
    return true;
}

} // namespace vxr