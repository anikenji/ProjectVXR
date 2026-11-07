#include "vxr_capture.h"
#include "DxgiDuplicator.h"

extern "C" {

VxrCaptureHandle vxr_capture_create(void) {
    return new (std::nothrow) vxr::DxgiDuplicator();
}

VxrCaptureStatus vxr_capture_init(VxrCaptureHandle handle, uint32_t adapter_index, uint32_t output_index) {
    if (!handle) return VXR_CAPTURE_INVALID_ARG;
    auto duplicator = static_cast<vxr::DxgiDuplicator*>(handle);
    return duplicator->Initialize(adapter_index, output_index);
}

VxrCaptureStatus vxr_capture_get_desc(VxrCaptureHandle handle, VxrCaptureDesc* out_desc) {
    if (!handle || !out_desc) return VXR_CAPTURE_INVALID_ARG;
    auto duplicator = static_cast<vxr::DxgiDuplicator*>(handle);
    return duplicator->GetDesc(out_desc);
}

VxrCaptureStatus vxr_capture_acquire_frame(VxrCaptureHandle handle, uint32_t timeout_ms, VxrCapturedFrame* out_frame) {
    if (!handle || !out_frame) return VXR_CAPTURE_INVALID_ARG;
    auto duplicator = static_cast<vxr::DxgiDuplicator*>(handle);
    return duplicator->AcquireFrame(timeout_ms, out_frame);
}

VxrCaptureStatus vxr_capture_release_frame(VxrCaptureHandle handle) {
    if (!handle) return VXR_CAPTURE_INVALID_ARG;
    auto duplicator = static_cast<vxr::DxgiDuplicator*>(handle);
    return duplicator->ReleaseFrame();
}

VxrCaptureStatus vxr_capture_reinit(VxrCaptureHandle handle) {
    if (!handle) return VXR_CAPTURE_INVALID_ARG;
    auto duplicator = static_cast<vxr::DxgiDuplicator*>(handle);
    return duplicator->Reinitialize();
}

void vxr_capture_destroy(VxrCaptureHandle handle) {
    if (handle) {
        auto duplicator = static_cast<vxr::DxgiDuplicator*>(handle);
        delete duplicator;
    }
}

} // extern "C"
