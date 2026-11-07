#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VxrMediaCodecDecoder.h"
#include "VxrVulkanRenderer.h"
#include "VxrOpenXrClient.h"
#include "VxrHardwareCursor.h"
#include "VxrQuestApp.h"

#include <iostream>
#include <cassert>
#include <cmath>

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

        // Queue simulated HEVC NALU
        uint8_t fakeNalu[256] = { 0x00, 0x00, 0x00, 0x01, 0x40, 0x01 };
        bool queued = decoder.QueueInputBuffer(fakeNalu, sizeof(fakeNalu), 1000000, true);
        assert(queued);

        // Dequeue output buffer containing AHardwareBuffer
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

        // Update position to (75%, 25%) on virtual screen
        cursor.UpdatePosition(0.75f, 0.25f, true);
        assert(cursor.GetNormalizedX() == 0.75f);
        assert(cursor.GetNormalizedY() == 0.25f);
        assert(cursor.GetUpdateCount() == 1);

        // Calculate 3D billboard vertices over desktop at (0, 1.5m, -2.0m)
        float desktopPos[3] = { 0.0f, 1.5f, -2.0f };
        float desktopW = 2.0f;  // 2 meters wide
        float desktopH = 1.125f; // 16:9 aspect ratio

        vxr::CursorQuadVertex quad[4];
        cursor.CalculateQuadVertices(desktopPos, desktopW, desktopH, quad);

        // Normalized x = 0.75 -> offset_x = (0.75 - 0.5) * 2.0 = +0.5m -> X center = 0.5m
        // Normalized y = 0.25 -> offset_y = (0.5 - 0.25) * 1.125 = +0.28125m -> Y center = 1.78125m
        float expectedCenterX = 0.5f;
        float expectedCenterY = 1.5f + 0.28125f;
        float actualCenterX = (quad[0].pos[0] + quad[2].pos[0]) * 0.5f;
        float actualCenterY = (quad[0].pos[1] + quad[2].pos[1]) * 0.5f;

        assert(std::abs(actualCenterX - expectedCenterX) < 1e-4);
        assert(std::abs(actualCenterY - expectedCenterY) < 1e-4);
        // Positioned 2mm in front of desktop screen to prevent Z-fighting
        assert(std::abs(quad[0].pos[2] - (-2.0f + 0.002f)) < 1e-4);

        std::cout << "  -> 0ms Cursor Quad projected at VR coordinates: ("
                  << actualCenterX << ", " << actualCenterY << ", " << quad[0].pos[2] << ")" << std::endl;
        std::cout << "  -> UV mapping verified: (" << quad[0].uv[0] << ", " << quad[0].uv[1]
                  << ") to (" << quad[2].uv[0] << ", " << quad[2].uv[1] << ")" << std::endl;
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
        // Verify IPD: Left eye at -0.0315m, Right eye at +0.0315m (0.063m total)
        assert(std::abs(views[0].position[0] - (-0.0315f)) < 1e-4);
        assert(std::abs(views[1].position[0] - 0.0315f) < 1e-4);
        std::cout << "  -> 6DoF Eye Views located: Left X=" << views[0].position[0]
                  << ", Right X=" << views[1].position[0] << " (IPD = 63mm)" << std::endl;

        bool endOk = openXr.EndFrame(displayTime);
        assert(endOk);
        assert(openXr.GetFrameIndex() == 1);

        openXr.HandleSessionStateChanged(vxr::OpenXrSessionState::Stopping);
        assert(!openXr.IsSessionRunning());
        std::cout << "  -> OpenXR session lifecycle validated." << std::endl;
    }

    // Test 5: End-to-End Quest App Coordinator Loop
    std::cout << "[Test 5] Testing End-to-End Quest App Coordinator (RunFrame loop)..." << std::endl;
    {
        vxr::VxrQuestApp app;
        vxr::QuestAppConfig config;
        config.preferred_codec = vxr::VideoCodecType::Hevc;
        config.stream_width = 2064;
        config.stream_height = 2208;
        config.target_fps = 90;
        config.enable_zero_copy = true;
        config.enable_space_warp = true;
        config.enable_foveation = true;

        bool initOk = app.Initialize(config);
        assert(initOk);
        assert(app.IsRunning());

        // Feed video packets
        uint8_t packet[128] = { 0x00, 0x00, 0x00, 0x01 };
        bool feedOk = app.FeedVideoData(packet, sizeof(packet), 123456, true);
        assert(feedOk);

        // Update hardware cursor
        app.UpdateHardwareCursor(0.8f, 0.4f, true);

        // Run 5 consecutive frames through the zero-copy pipeline
        for (int i = 0; i < 5; ++i) {
            bool frameOk = app.RunFrame();
            assert(frameOk);
        }

        assert(app.GetTotalFramesRendered() == 5);
        std::cout << "  -> 5 consecutive frames rendered through zero-copy pipeline!" << std::endl;

        app.Shutdown();
        assert(!app.IsRunning());
        std::cout << "  -> Quest App clean shutdown verified." << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL QUEST CLIENT TESTS PASSED!                " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
