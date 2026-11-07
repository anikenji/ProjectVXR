#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VxrMediaCodecDecoder.h"
#include "VxrVulkanRenderer.h"
#include "VxrOpenXrClient.h"
#include "VxrHardwareCursor.h"
#include "VxrVrMenuUi.h"
#include "VxrClientNetwork.h"
#include "VxrQuestApp.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: Quest Client Test Suite (NDK)    " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: AMediaCodec Decoder (KEY_LOW_LATENCY)
    std::cout << "[Test 1] Testing AMediaCodec Low-Latency configuration & output..." << std::endl;
    {
        vxr::VxrMediaCodecDecoder decoder;
        bool initOk = decoder.Initialize(vxr::VideoCodecType::Hevc, 2064, 2208, 90);
        assert(initOk);
        assert(decoder.IsInitialized());
        assert(decoder.IsLowLatencyEnabled());
        assert(decoder.GetWidth() == 2064 && decoder.GetHeight() == 2208);
        assert(decoder.GetFps() == 90);

        uint8_t fakeNalu[256] = { 0x00, 0x00, 0x00, 0x01, 0x40, 0x01 };
        bool queued = decoder.QueueInputBuffer(fakeNalu, sizeof(fakeNalu), 1000000, true);
        assert(queued);

        vxr::DecodedOutputFrame frame;
        vxr::DecoderStatus status = decoder.DequeueOutputBuffer(&frame, 0);
        assert(status == vxr::DecoderStatus::Success);
        assert(frame.hardware_buffer != nullptr);
        assert(frame.width == 2064 && frame.height == 2208);
        std::cout << "  -> AMediaCodec frame decoded: buffer_idx=" << frame.buffer_index
                  << ", hardware_buffer=" << frame.hardware_buffer << std::endl;

        bool released = decoder.ReleaseOutputBuffer(frame.buffer_index, false);
        assert(released);
        std::cout << "  -> KEY_LOW_LATENCY & AHardwareBuffer output verified." << std::endl;
    }

    // Test 2: Vulkan 1.3 Zero-Copy AHardwareBuffer Texture Import
    std::cout << "[Test 2] Testing Vulkan 1.3 zero-copy AHardwareBuffer texture binding..." << std::endl;
    {
        vxr::VxrVulkanRenderer renderer;
        vxr::VulkanRendererConfig config;
        config.width = 2064;
        config.height = 2208;
        config.enable_validation_layers = false;
        config.enable_zero_copy_ahb = true;
        config.msaa_samples = 4;

        bool initOk = renderer.Initialize(config);
        assert(initOk);
        assert(renderer.IsZeroCopySupported());

        void* mockAhb = reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEFULL));
        vxr::VulkanTexture texture = {};
        bool importOk = renderer.ImportHardwareBuffer(mockAhb, 2064, 2208, &texture);
        assert(importOk);
        assert(texture.image != nullptr);
        assert(texture.image_view != nullptr);
        assert(texture.memory != nullptr);
        assert(texture.sampler != nullptr);
        assert(texture.is_external_ycbcr);
        assert(texture.native_hardware_buffer == mockAhb);
        std::cout << "  -> Zero-Copy: AHardwareBuffer successfully bound to VkImage (Format="
                  << texture.format << ", YCbCr=true)" << std::endl;

        bool beginOk = renderer.BeginFrame();
        assert(beginOk);
        renderer.RenderStreamQuad(texture, 0); // left eye
        renderer.RenderStreamQuad(texture, 1); // right eye
        renderer.RenderMenuOverlayQuad(true, true);
        bool endOk = renderer.EndFrame();
        assert(endOk);
        assert(renderer.GetRenderedFrameCount() == 1);

        renderer.ReleaseTexture(&texture);
        assert(texture.image == nullptr);
        std::cout << "  -> Zero-Copy Vulkan render cycle verified." << std::endl;
    }

    // Test 3: 0ms Hardware Cursor Plane Projection
    std::cout << "[Test 3] Testing 0ms Local Hardware Cursor plane projection..." << std::endl;
    {
        vxr::VxrHardwareCursor cursor;
        assert(cursor.IsVisible());

        cursor.UpdatePosition(0.75f, 0.25f, true);
        assert(cursor.GetNormalizedX() == 0.75f);
        assert(cursor.GetNormalizedY() == 0.25f);
        assert(cursor.GetUpdateCount() == 1);

        float desktopPos[3] = { 0.0f, 1.5f, -2.0f };
        float desktopW = 2.0f;
        float desktopH = 1.125f;

        vxr::CursorQuadVertex quad[4];
        cursor.CalculateQuadVertices(desktopPos, desktopW, desktopH, quad);

        float expectedCenterX = 0.5f;
        float expectedCenterY = 1.5f + 0.28125f;
        float actualCenterX = (quad[0].pos[0] + quad[2].pos[0]) * 0.5f;
        float actualCenterY = (quad[0].pos[1] + quad[2].pos[1]) * 0.5f;

        assert(std::abs(actualCenterX - expectedCenterX) < 1e-4);
        assert(std::abs(actualCenterY - expectedCenterY) < 1e-4);
        assert(std::abs(quad[0].pos[2] - (-2.0f + 0.002f)) < 1e-4);

        std::cout << "  -> 0ms Cursor Quad projected at VR coordinates: ("
                  << actualCenterX << ", " << actualCenterY << ", " << quad[0].pos[2] << ")" << std::endl;
    }

    // Test 4: OpenXR Session Lifecycle & 6DoF Views
    std::cout << "[Test 4] Testing OpenXR Session Lifecycle and 6DoF tracking views..." << std::endl;
    {
        vxr::VxrOpenXrClient openXr;
        vxr::OpenXrClientConfig config;
        config.app_name = "VXR Test App";
        config.display_refresh_rate = 90;
        config.enable_space_warp_ssw = true;
        config.enable_foveated_rendering = true;

        bool initOk = openXr.Initialize(config);
        assert(initOk);
        assert(openXr.IsSessionRunning());
        assert(openXr.IsFocused());
        assert(openXr.IsSpaceWarpEnabled());

        uint64_t displayTime = 0;
        bool waitOk = openXr.WaitFrame(&displayTime);
        assert(waitOk);
        assert(displayTime > 0);

        bool beginOk = openXr.BeginFrame();
        assert(beginOk);

        vxr::XrEyeView views[2];
        bool locateOk = openXr.LocateViews(displayTime, views);
        assert(locateOk);
        assert(std::abs(views[0].position[0] - (-0.0315f)) < 1e-4);
        assert(std::abs(views[1].position[0] - 0.0315f) < 1e-4);

        float u = 0.0f, v = 0.0f;
        assert(openXr.ComputeDesktopPointerUV(2.2f, &u, &v));
        assert(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f);

        bool endOk = openXr.EndFrame(displayTime);
        assert(endOk);
        assert(openXr.GetFrameIndex() == 1);

        openXr.HandleSessionStateChanged(vxr::OpenXrSessionState::Stopping);
        assert(!openXr.IsSessionRunning());
        std::cout << "  -> OpenXR session lifecycle & laser ray-plane UV intersection validated." << std::endl;
    }

    // Test 5: Virtual Desktop 7-Tab In-VR Menu UI & RGBA Framebuffer Rasterizer
    std::cout << "[Test 5] Testing Virtual Desktop 7-Tab In-VR Menu UI & Performance HUD..." << std::endl;
    {
        vxr::VxrVrMenuUi menu;
        assert(menu.Initialize());
        assert(menu.IsVisible());
        assert(menu.GetVrRenderWidth() == 2496 && menu.GetVrRenderHeight() == 2592); // High preset default
        assert(vxr::VxrVrMenuUi::GetVrRenderWidthForQuality(5) == 3072); // Godlike preset

        std::vector<uint32_t> rgba(1280 * 800, 0);
        for (uint32_t tabIdx = 0; tabIdx < 7; ++tabIdx) {
            menu.SetActiveTab(static_cast<vxr::VrMenuTab>(tabIdx));
            assert(menu.RenderMenuToRgba(rgba.data(), 1280, 800));
        }

        vxr::VrPerfStats stats;
        std::vector<uint32_t> hudRgba(480 * 220, 0);
        assert(menu.RenderPerformanceOverlayToRgba(hudRgba.data(), 480, 220, stats));
        std::cout << "  -> All 7 VR Menu Tabs (COMPUTERS..VIDEOS) & Performance Overlay rasterized cleanly." << std::endl;
    }

    // Test 6: Multi-Fragment UDP Video Frame Reassembly & Bidirectional Control Protocol
    std::cout << "[Test 6] Testing Multi-Fragment UDP Video Reassembly & Control Protocol..." << std::endl;
    {
        vxr::VxrClientNetwork net;
        assert(net.Initialize(0, 0));

        // Simulate a 3000-byte HEVC keyframe split across 3 MTU fragments (1200 + 1200 + 600)
        std::vector<uint8_t> originalFrame(3000);
        for (size_t i = 0; i < originalFrame.size(); ++i) originalFrame[i] = static_cast<uint8_t>(i & 0xFF);

        const uint16_t fragCount = 3;
        const size_t sizes[3] = { 1200, 1200, 600 };
        vxr::ReassembledVideoFrame outFrame = {};
        bool completed = false;

        for (uint16_t f = 0; f < fragCount; ++f) {
            std::vector<uint8_t> pkt(sizeof(vxr::VxrUdpVideoFragmentHeader) + sizes[f]);
            vxr::VxrUdpVideoFragmentHeader hdr = {};
            hdr.magic = vxr::VXR_UDP_VIDEO_MAGIC;
            hdr.frame_id = 42;
            hdr.frag_idx = f;
            hdr.frag_count = fragCount;
            hdr.flags = 0x01; // keyframe
            hdr.codec_id = 1; // HEVC
            hdr.payload_len = static_cast<uint16_t>(sizes[f]);
            hdr.pts_us = 777000;
            hdr.cursor_norm_x = 0.65f;
            hdr.cursor_norm_y = 0.35f;
            hdr.cursor_visible = 1;

            std::memcpy(pkt.data(), &hdr, sizeof(hdr));
            std::memcpy(pkt.data() + sizeof(hdr), originalFrame.data() + f * 1200, sizes[f]);

            completed = net.ReassembleFrame(pkt.data(), pkt.size(), &outFrame);
            if (f < fragCount - 1) assert(!completed);
        }

        assert(completed);
        assert(outFrame.size == 3000);
        assert(outFrame.frame_id == 42);
        assert(outFrame.is_keyframe);
        assert(std::memcmp(outFrame.data.data(), originalFrame.data(), 3000) == 0);
        std::cout << "  -> 3-fragment 3000-byte HEVC frame reassembled with 100% byte accuracy." << std::endl;
        net.Cleanup();
    }

    // Test 7: End-to-End Quest App Coordinator Loop
    std::cout << "[Test 7] Testing End-to-End Quest App Coordinator (RunFrame loop)..." << std::endl;
    {
        vxr::VxrQuestApp app;
        vxr::QuestAppConfig config;
        config.preferred_codec = vxr::VideoCodecType::Hevc;
        config.stream_width = 2496;
        config.stream_height = 2592;
        config.target_fps = 90;
        config.enable_zero_copy = true;
        config.enable_space_warp = true;
        config.enable_foveation = true;

        bool initOk = app.Initialize(config);
        assert(initOk);
        assert(app.IsRunning());

        uint8_t packet[128] = { 0x00, 0x00, 0x00, 0x01 };
        bool feedOk = app.FeedVideoData(packet, sizeof(packet), 123456, true);
        assert(feedOk);

        app.UpdateHardwareCursor(0.8f, 0.4f, true);

        for (int i = 0; i < 5; ++i) {
            bool frameOk = app.RunFrame();
            assert(frameOk);
        }

        assert(app.GetTotalFramesRendered() == 5);
        std::cout << "  -> 5 consecutive frames rendered with 7-Tab Menu & Performance HUD layers!" << std::endl;

        app.Shutdown();
        assert(!app.IsRunning());
        std::cout << "  -> Quest App clean shutdown verified." << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL 7 QUEST CLIENT TEST SUITES PASSED!        " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
