#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VxrIddAdapter.h"
#include "VxrEdidSynthesizer.h"
#include "vxr_iddcx_control.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: IDDCX Virtual Display Driver Test" << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: EDID Synthesis & VESA Checksum Validation
    std::cout << "[Test 1] Testing EDID Synthesis & VESA Specification Validation..." << std::endl;
    {
        uint8_t edid[VXR_IDD_EDID_BLOCK_SIZE];

        // Synthesize EDID for Monitor 1 (4K @ 120Hz preferred)
        bool synthOk = vxr::VxrEdidSynthesizer::Synthesize(1, 3840, 2160, 120, edid);
        assert(synthOk);

        // Verify VESA header
        const uint8_t vesaHeader[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
        assert(memcmp(edid, vesaHeader, 8) == 0);

        // Verify Manufacturer ID "VXR" (0x5B12)
        assert(edid[8] == 0x5B && edid[9] == 0x12);

        // Verify Product ID (1)
        assert(edid[10] == 0x01 && edid[11] == 0x00);

        // Verify Version 1.4
        assert(edid[18] == 1 && edid[19] == 4);

        // Verify Checksum modulo 256 is 0
        uint32_t sum = 0;
        for (int i = 0; i < VXR_IDD_EDID_BLOCK_SIZE; ++i) {
            sum += edid[i];
        }
        assert((sum % 256) == 0);
        assert(vxr::VxrEdidSynthesizer::Validate(edid));

        // Test corruption detection
        edid[50] ^= 0xFF;
        assert(!vxr::VxrEdidSynthesizer::Validate(edid));

        std::cout << "  -> 128-byte VESA EDID synthesized & validated (Manufacturer=VXR, Checksum=OK)." << std::endl;
    }

    // Test 2: Virtual Headless Monitor Creation (1 to 3 monitors)
    std::cout << "[Test 2] Testing Virtual Monitor Creation (1 to 3 concurrent monitors)..." << std::endl;
    {
        auto& adapter = vxr::VxrIddAdapter::GetInstance();
        adapter.DestroyAllMonitors();
        assert(adapter.GetMonitorCount() == 0);

        // Create Monitor 1: 1080p @ 120Hz
        uint32_t mon1 = adapter.CreateMonitor(1920, 1080, 120);
        assert(mon1 == 1);
        assert(adapter.GetMonitorCount() == 1);

        // Create Monitor 2: 1440p @ 90Hz
        uint32_t mon2 = adapter.CreateMonitor(2560, 1440, 90);
        assert(mon2 == 2);
        assert(adapter.GetMonitorCount() == 2);

        // Create Monitor 3: 4K @ 60Hz
        uint32_t mon3 = adapter.CreateMonitor(3840, 2160, 60);
        assert(mon3 == 3);
        assert(adapter.GetMonitorCount() == 3);

        // Attempting to create Monitor 4 should fail (capped at 3 maximum monitors)
        uint32_t mon4 = adapter.CreateMonitor(1920, 1080, 60);
        assert(mon4 == 0);
        assert(adapter.GetMonitorCount() == 3);
        std::cout << "  -> Successfully created 3 virtual monitors; properly enforced MAX=3 limit." << std::endl;

        // Verify descriptors
        VxrMonitorDesc descs[4];
        uint32_t enumerated = adapter.GetAllMonitorDescriptors(descs, 4);
        assert(enumerated == 3);
        assert(descs[0].monitor_id == 1 && descs[0].current_width == 1920);
        assert(descs[1].monitor_id == 2 && descs[1].current_width == 2560);
        assert(descs[2].monitor_id == 3 && descs[2].current_width == 3840);
        std::cout << "  -> Multi-monitor enumeration verified (1080p, 1440p, 4K)." << std::endl;
    }

    // Test 3: Custom Resolutions up to 4K & Refresh Rates (90Hz / 120Hz)
    std::cout << "[Test 3] Testing dynamic resolution and refresh rate switching (up to 4K @ 120Hz)..." << std::endl;
    {
        auto& adapter = vxr::VxrIddAdapter::GetInstance();
        auto mon1 = adapter.GetMonitor(1);
        assert(mon1 != nullptr);

        // Switch Monitor 1 from 1080p 120Hz to 4K @ 120Hz
        bool modeOk = mon1->SetMode(3840, 2160, 120);
        assert(modeOk);
        assert(mon1->GetWidth() == 3840);
        assert(mon1->GetHeight() == 2160);
        assert(mon1->GetRefreshRate() == 120);

        // Check updated EDID
        assert(vxr::VxrEdidSynthesizer::Validate(mon1->GetEdid()));
        std::cout << "  -> Dynamically switched Monitor #1 to 3840x2160 (4K) @ 120Hz!" << std::endl;
    }

    // Test 4: Swapchain Buffer Acquisition
    std::cout << "[Test 4] Testing Swapchain buffer acquisition and shared handle access..." << std::endl;
    {
        auto& adapter = vxr::VxrIddAdapter::GetInstance();
        auto mon1 = adapter.GetMonitor(1);
        assert(mon1 != nullptr);

        VxrIddFrame frame;
        bool acquireOk = mon1->AcquireNextFrame(16, &frame);
        assert(acquireOk);
        assert(frame.has_new_frame);
        assert(frame.d3d11_texture != nullptr);
        assert(frame.shared_handle != nullptr);
        assert(frame.width == 3840 && frame.height == 2160);
        assert(frame.frame_index == 1);
        std::cout << "  -> Acquired 4K frame: texture=" << frame.d3d11_texture
                  << ", shared_handle=" << frame.shared_handle << std::endl;

        bool releaseOk = mon1->ReleaseFrame();
        assert(releaseOk);
        std::cout << "  -> Frame released back to DWM compositor successfully." << std::endl;
    }

    // Test 5: User-Mode C API & Monitor Deletion
    std::cout << "[Test 5] Testing User-Mode Control C API..." << std::endl;
    {
        assert(VxrIdd_GetMonitorCount() == 3);

        // Destroy Monitor 2
        bool destroyed = VxrIdd_DestroyMonitor(2);
        assert(destroyed);
        assert(VxrIdd_GetMonitorCount() == 2);

        // Re-create: Should reuse ID 2
        uint32_t newId = VxrIdd_CreateMonitor(2560, 1440, 120);
        assert(newId == 2);
        assert(VxrIdd_GetMonitorCount() == 3);

        // Query info via C API
        VxrMonitorDesc desc;
        bool infoOk = VxrIdd_GetMonitorInfo(2, &desc);
        assert(infoOk);
        assert(desc.monitor_id == 2);
        assert(desc.current_width == 2560 && desc.current_height == 1440 && desc.current_refresh_rate == 120);

        // Cleanup all
        auto& adapter = vxr::VxrIddAdapter::GetInstance();
        adapter.DestroyAllMonitors();
        assert(VxrIdd_GetMonitorCount() == 0);
        std::cout << "  -> C API monitor creation, querying, deletion, and slot reuse verified." << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL IDDCX DRIVER TESTS PASSED!                " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
