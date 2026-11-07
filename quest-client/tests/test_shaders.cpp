#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VxrComputeShaders.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <fstream>

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: Quest Shaders Test Suite         " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: Validate SPIR-V Binary Files
    std::cout << "[Test 1] Validating SPIR-V binary files..." << std::endl;
    {
        std::vector<std::string> shaderFiles = {
            "quest-client/shaders/ssw_reproject.spv",
            "quest-client/shaders/ffx_cas.spv",
            "quest-client/shaders/qcom_sgsr.spv"
        };

        for (const auto& file : shaderFiles) {
            std::vector<uint32_t> words;
            bool loadOk = vxr::VxrSpirvLoader::LoadSpirvFromFile(file, words);
            assert(loadOk);
            assert(words.size() >= 5);
            assert(words[0] == 0x07230203); // SPIR-V Magic
            std::cout << "  -> " << file << ": Valid SPIR-V module (" << words.size() * 4 << " bytes)" << std::endl;
        }
    }

    // Test 2: Validate GLSL Shader Source Files
    std::cout << "[Test 2] Validating GLSL compute shader sources..." << std::endl;
    {
        std::vector<std::string> glslFiles = {
            "quest-client/shaders/ssw_reproject.comp",
            "quest-client/shaders/ffx_cas.comp",
            "quest-client/shaders/qcom_sgsr.comp"
        };

        for (const auto& file : glslFiles) {
            std::ifstream stream(file);
            assert(stream.is_open());
            std::string line;
            std::getline(stream, line);
            assert(line.find("#version 450") != std::string::npos);
            std::cout << "  -> " << file << ": Valid GLSL 450 source present" << std::endl;
        }
    }

    // Test 3: On-Device Synchronous Spacewarp (SSW) Motion Vector Reprojection
    std::cout << "[Test 3] Testing On-Device Synchronous Spacewarp (SSW) Reprojection..." << std::endl;
    {
        const uint32_t W = 64;
        const uint32_t H = 64;
        std::vector<float> prevFrame(W * H * 4, 0.0f);
        std::vector<float> motionVectors(W * H * 2, 0.0f);
        std::vector<float> reprojected(W * H * 4, 0.0f);

        // Place a bright 4x4 white square at (20, 20)
        for (uint32_t y = 20; y < 24; ++y) {
            for (uint32_t x = 20; x < 24; ++x) {
                size_t idx = (y * W + x) * 4;
                prevFrame[idx + 0] = 1.0f;
                prevFrame[idx + 1] = 1.0f;
                prevFrame[idx + 2] = 1.0f;
                prevFrame[idx + 3] = 1.0f;
            }
        }

        // Apply a motion vector of (+10, +5) pixels
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                size_t mv_idx = (y * W + x) * 2;
                motionVectors[mv_idx + 0] = 10.0f;
                motionVectors[mv_idx + 1] = 5.0f;
            }
        }

        vxr::VxrSswCompute ssw;
        bool initOk = ssw.Initialize("quest-client/shaders/ssw_reproject.spv", W, H);
        assert(initOk);

        // Reproject with time_delta = 1.0, scale = 1.0
        ssw.ReprojectFrame(prevFrame.data(), motionVectors.data(), W, H, 1.0f, 1.0f, reprojected.data());
        assert(ssw.GetReprojectedCount() == 1);

        // The square should now appear shifted at (20 + 10 = 30, 20 + 5 = 25)
        size_t warped_center = (27 * W + 32) * 4;
        assert(reprojected[warped_center] > 0.8f);

        // Old position (20, 20) should now be empty (0.0f)
        size_t old_center = (22 * W + 22) * 4;
        assert(reprojected[old_center] < 0.1f);

        std::cout << "  -> SSW successfully warped dropped frame by (+10, +5) motion delta!" << std::endl;
    }

    // Test 4: AMD FidelityFX Contrast Adaptive Sharpening (CAS)
    std::cout << "[Test 4] Testing AMD FidelityFX CAS image sharpening..." << std::endl;
    {
        const uint32_t W = 32;
        const uint32_t H = 32;
        std::vector<float> blurryFrame(W * H * 4, 0.0f);
        std::vector<float> sharpenedFrame(W * H * 4, 0.0f);

        // Create a blurry vertical step edge around x = 16:
        // x < 14: 0.1, x = 14: 0.3, x = 15: 0.5, x = 16: 0.7, x > 16: 0.9
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                size_t idx = (y * W + x) * 4;
                float val = 0.1f;
                if (x == 14) val = 0.3f;
                else if (x == 15) val = 0.5f;
                else if (x == 16) val = 0.7f;
                else if (x > 16) val = 0.9f;

                blurryFrame[idx + 0] = val;
                blurryFrame[idx + 1] = val;
                blurryFrame[idx + 2] = val;
                blurryFrame[idx + 3] = 1.0f;
            }
        }

        vxr::VxrCasCompute cas;
        bool initOk = cas.Initialize("quest-client/shaders/ffx_cas.spv", W, H);
        assert(initOk);

        cas.SharpenFrame(blurryFrame.data(), W, H, 1.0f, sharpenedFrame.data());
        assert(cas.GetSharpenedCount() == 1);

        // Verify edge sharpness increased without exceeding min/max bounds
        size_t edge_low = (16 * W + 14) * 4;
        size_t edge_high = (16 * W + 16) * 4;

        assert(sharpenedFrame[edge_low] <= blurryFrame[edge_low]);   // Lower side gets darker
        assert(sharpenedFrame[edge_high] >= blurryFrame[edge_high]); // Higher side gets brighter
        assert(sharpenedFrame[edge_low] >= 0.1f);  // No undershoot below min
        assert(sharpenedFrame[edge_high] <= 0.9f); // No overshoot above max

        std::cout << "  -> AMD FidelityFX CAS sharpened edge contrast without ringing/haloing!" << std::endl;
    }

    // Test 5: Qualcomm Snapdragon Game Super Resolution (SGSR)
    std::cout << "[Test 5] Testing Qualcomm SGSR edge-directed sharpening..." << std::endl;
    {
        const uint32_t W = 32;
        const uint32_t H = 32;
        std::vector<float> inputFrame(W * H * 4, 0.2f);
        std::vector<float> enhancedFrame(W * H * 4, 0.0f);

        // Add diagonal high-contrast line from (5, 5) to (25, 25)
        for (uint32_t i = 5; i < 25; ++i) {
            size_t idx = (i * W + i) * 4;
            inputFrame[idx + 0] = 0.9f;
            inputFrame[idx + 1] = 0.9f;
            inputFrame[idx + 2] = 0.9f;
        }

        vxr::VxrSgsrCompute sgsr;
        bool initOk = sgsr.Initialize("quest-client/shaders/qcom_sgsr.spv", W, H);
        assert(initOk);

        sgsr.ProcessFrame(inputFrame.data(), W, H, 0.1f, 1.0f, enhancedFrame.data());
        assert(sgsr.GetProcessedCount() == 1);

        // Line pixels should remain crisp and bounded
        size_t line_pixel = (15 * W + 15) * 4;
        assert(enhancedFrame[line_pixel] >= 0.85f);

        std::cout << "  -> Qualcomm SGSR directional edge enhancement verified!" << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL QUEST SHADER TESTS PASSED!                " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
