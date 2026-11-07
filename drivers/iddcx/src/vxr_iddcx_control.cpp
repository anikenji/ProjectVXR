#include "vxr_iddcx_control.h"
#include "VxrIddAdapter.h"
#include "VxrEdidSynthesizer.h"

extern "C" {

uint32_t VxrIdd_CreateMonitor(uint32_t width, uint32_t height, uint32_t refresh_rate) {
    return vxr::VxrIddAdapter::GetInstance().CreateMonitor(width, height, refresh_rate);
}

bool VxrIdd_DestroyMonitor(uint32_t monitor_id) {
    return vxr::VxrIddAdapter::GetInstance().DestroyMonitor(monitor_id);
}

bool VxrIdd_SetResolution(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate) {
    auto monitor = vxr::VxrIddAdapter::GetInstance().GetMonitor(monitor_id);
    if (!monitor) return false;
    return monitor->SetMode(width, height, refresh_rate);
}

uint32_t VxrIdd_GetMonitorCount(void) {
    return vxr::VxrIddAdapter::GetInstance().GetMonitorCount();
}

bool VxrIdd_GetMonitorInfo(uint32_t monitor_id, VxrMonitorDesc* out_desc) {
    auto monitor = vxr::VxrIddAdapter::GetInstance().GetMonitor(monitor_id);
    if (!monitor) return false;
    return monitor->GetDescriptor(out_desc);
}

bool VxrIdd_AcquireNextFrame(uint32_t monitor_id, uint32_t timeout_ms, VxrIddFrame* out_frame) {
    auto monitor = vxr::VxrIddAdapter::GetInstance().GetMonitor(monitor_id);
    if (!monitor) return false;
    return monitor->AcquireNextFrame(timeout_ms, out_frame);
}

bool VxrIdd_ReleaseFrame(uint32_t monitor_id) {
    auto monitor = vxr::VxrIddAdapter::GetInstance().GetMonitor(monitor_id);
    if (!monitor) return false;
    return monitor->ReleaseFrame();
}

bool VxrIdd_SynthesizeEdid(
    uint32_t monitor_id,
    uint32_t width,
    uint32_t height,
    uint32_t refresh_rate,
    uint8_t out_edid[VXR_IDD_EDID_BLOCK_SIZE]
) {
    return vxr::VxrEdidSynthesizer::Synthesize(monitor_id, width, height, refresh_rate, out_edid);
}

bool VxrIdd_ValidateEdid(const uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE]) {
    return vxr::VxrEdidSynthesizer::Validate(edid);
}

} // extern "C"
