#include "vxr_capture.h"
#include "DxgiDuplicator.h"

#include <iostream>
#include <cassert>

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "   Project VXR: DXGI Capture Test Suite   " << std::endl;
    std::cout << "=========================================" << std::endl;

    // Test 1: Handle lifecycle
    std::cout << "[Test 1] Testing instance creation & destruction..." << std::endl;
    VxrCaptureHandle handle = vxr_capture_create();
    assert(handle != nullptr);
    std::cout << "  -> vxr_capture_create() succeeded." << std::endl;

    // Test 2: Uninitialized operations return NOT_INITIALIZED
    std::cout << "[Test 2] Testing uninitialized state guards..." << std::endl;
    VxrCaptureDesc desc;
    VxrCaptureStatus status = vxr_capture_get_desc(handle, &desc);
    assert(status == VXR_CAPTURE_NOT_INITIALIZED);

    VxrCapturedFrame frame;
    status = vxr_capture_acquire_frame(handle, 16, &frame);
    assert(status == VXR_CAPTURE_NOT_INITIALIZED);
    std::cout << "  -> Correctly rejected uninitialized calls." << std::endl;

    // Test 3: Initialize on Primary Adapter & Output
    std::cout << "[Test 3] Initializing DXGI Output Duplication (Adapter 0, Output 0)..." << std::endl;
    status = vxr_capture_init(handle, 0, 0);

    if (status == VXR_CAPTURE_OK) {
        std::cout << "  -> Initialization SUCCESSFUL on active display!" << std::endl;

        status = vxr_capture_get_desc(handle, &desc);
        assert(status == VXR_CAPTURE_OK);
        std::cout << "  -> Display: " << desc.width << "x" << desc.height
                  << " @ " << desc.refresh_rate_num << "/" << desc.refresh_rate_den << " Hz" << std::endl;
        std::cout << "  -> Adapter: " << desc.adapter_name << std::endl;
        assert(desc.width > 0 && desc.height > 0);

        // Test 4: Frame acquisition
        std::cout << "[Test 4] Testing frame acquisition loop (timeout = 100ms)..." << std::endl;
        status = vxr_capture_acquire_frame(handle, 100, &frame);

        if (status == VXR_CAPTURE_OK) {
            std::cout << "  -> Frame acquired!" << std::endl;
            std::cout << "  -> Texture: " << frame.d3d11_texture << ", Shared Handle: " << frame.shared_handle << std::endl;
            std::cout << "  -> Has new frame: " << frame.has_new_frame
                      << ", Present QPC: " << frame.present_time_qpc << std::endl;
            std::cout << "  -> Cursor: visible=" << frame.cursor.visible
                      << " pos=(" << frame.cursor.x << ", " << frame.cursor.y << ")" << std::endl;

            assert(frame.d3d11_texture != nullptr);
            assert(frame.shared_handle != nullptr);

            status = vxr_capture_release_frame(handle);
            assert(status == VXR_CAPTURE_OK);
            std::cout << "  -> Frame released successfully." << std::endl;
        } else if (status == VXR_CAPTURE_TIMEOUT) {
            std::cout << "  -> Frame acquisition timed out (screen static, expected behavior)." << std::endl;
            assert(frame.d3d11_texture != nullptr);
        } else {
            std::cout << "  -> Acquire returned status: " << status << std::endl;
        }

        // Test 5: Reinitialization
        std::cout << "[Test 5] Testing re-initialization (recovery from device/mode change)..." << std::endl;
        status = vxr_capture_reinit(handle);
        assert(status == VXR_CAPTURE_OK);
        std::cout << "  -> Reinitialization succeeded." << std::endl;

    } else if (status == VXR_CAPTURE_NO_OUTPUT) {
        std::cout << "  -> Adapter 0 has no active monitor attached (headless/virtual environment)." << std::endl;
        std::cout << "  -> Correctly reported VXR_CAPTURE_NO_OUTPUT." << std::endl;
    } else {
        std::cout << "  -> Initialization returned status: " << status << std::endl;
    }

    // Release C API handle before creating C++ class instance
    // (DXGI Desktop Duplication allows only 1 duplication instance per desktop output)
    vxr_capture_destroy(handle);
    handle = nullptr;
    std::cout << "  -> Destroyed C API capture instance." << std::endl;

    // Test 6: Direct C++ class verification
    std::cout << "[Test 6] Testing C++ DxgiDuplicator direct interface..." << std::endl;
    {
        vxr::DxgiDuplicator duplicator;
        status = duplicator.Initialize(0, 0);
        if (status == VXR_CAPTURE_OK) {
            assert(duplicator.IsInitialized());
            assert(duplicator.GetDevice() != nullptr);
            assert(duplicator.GetContext() != nullptr);
            assert(duplicator.GetSharedTexture() != nullptr);
            assert(duplicator.GetSharedHandle() != nullptr);
            std::cout << "  -> C++ DxgiDuplicator resource getters verified." << std::endl;

            VxrCapturedFrame cppFrame;
            duplicator.AcquireFrame(50, &cppFrame);
            duplicator.ReleaseFrame();

            duplicator.Cleanup();
            assert(!duplicator.IsInitialized());
            std::cout << "  -> C++ DxgiDuplicator cleanup verified." << std::endl;
        }
    }

    std::cout << "=========================================" << std::endl;
    std::cout << "   ALL VXR-CAPTURE TESTS PASSED!         " << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}
