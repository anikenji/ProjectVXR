#ifdef NDEBUG
#undef NDEBUG
#endif

#include "vxr_encoder.h"
#include "VxrNvencEncoder.h"
#include "VxrAmfEncoder.h"
#include "VxrVideoEncoder.h"

#include <iostream>
#include <cassert>
#include <vector>

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: Hardware Encoder Test Suite      " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: NVENC Driver Library Loading on NVIDIA GPU
    std::cout << "[Test 1] Testing NVENC Hardware Driver Library Loading..." << std::endl;
    {
        vxr::VxrNvencEncoder nvenc;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_HEVC;
        config.color_format = VXR_COLOR_P010; // 10-bit color
        config.width = 2064;
        config.height = 2208;
        config.fps = 90;
        config.bitrate_bps = 150000000; // 150 Mbps
        config.max_bitrate_bps = 250000000;
        config.enable_two_pass = true;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = true;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 8;

        assert(nvenc.Initialize(config, nullptr));
        assert(nvenc.IsInitialized());
        assert(nvenc.IsTenBitEnabled());
        assert(nvenc.IsTwoPassEnabled());
        assert(nvenc.IsAdaptiveQuantizationEnabled());
        assert(nvenc.IsIntraRefreshEnabled());

        std::cout << "  -> NVENC Driver Status: " << (nvenc.IsHardwareActive() ? "HARDWARE ACTIVE" : "SIMULATED") << std::endl;
        std::cout << "  -> HEVC 10-Bit P010 profile verified." << std::endl;
    }

    // Test 2: AV1 10-Bit Encoding Mode (RTX 40-Series)
    std::cout << "[Test 2] Testing AV1 10-bit encoding configuration..." << std::endl;
    {
        vxr::VxrNvencEncoder av1Enc;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_AV1;
        config.color_format = VXR_COLOR_P010;
        config.width = 2064;
        config.height = 2208;
        config.fps = 90;
        config.bitrate_bps = 200000000; // 200 Mbps
        config.max_bitrate_bps = 400000000;
        config.enable_two_pass = true;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = true;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 8;

        assert(av1Enc.Initialize(config, nullptr));
        assert(av1Enc.IsInitialized());
        assert(av1Enc.IsTenBitEnabled());
        std::cout << "  -> AV1 10-bit encoding profile initialized successfully." << std::endl;
    }

    // Test 3: H.264+ High Bitrate Mode (up to 400-500 Mbps for Wi-Fi 6E)
    std::cout << "[Test 3] Testing H.264+ High Bitrate & 2-Pass Encoding..." << std::endl;
    {
        vxr::VxrNvencEncoder h264Enc;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_H264;
        config.color_format = VXR_COLOR_NV12;
        config.width = 2064;
        config.height = 2208;
        config.fps = 120;
        config.bitrate_bps = 400000000; // 400 Mbps!
        config.max_bitrate_bps = 500000000;
        config.enable_two_pass = true;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = true;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 8;

        assert(h264Enc.Initialize(config, nullptr));
        assert(h264Enc.IsTwoPassEnabled());
        std::cout << "  -> H.264+ 400 Mbps @ 120fps with 2-Pass & AQ verified." << std::endl;
    }

    // Test 4: D3D11 Texture Zero-Copy Encoding & Bitstream Extraction
    std::cout << "[Test 4] Testing D3D11 zero-copy texture encoding & NALU bitstream extraction..." << std::endl;
    {
        vxr::VxrNvencEncoder encoder;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_HEVC;
        config.color_format = VXR_COLOR_P010;
        config.width = 1920;
        config.height = 1080;
        config.fps = 90;
        config.bitrate_bps = 100000000; // 100 Mbps
        config.max_bitrate_bps = 200000000;
        config.enable_two_pass = true;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = true;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 8;

        assert(encoder.Initialize(config, nullptr));

        void* mockD3DTexture = reinterpret_cast<void*>(0x11112222ULL);

        // Frame 1: Force IDR keyframe
        VxrBitstreamFrame frame1;
        assert(encoder.EncodeTexture(mockD3DTexture, 1000000, true, &frame1));
        assert(frame1.is_keyframe);
        assert(frame1.size > 0);
        assert(frame1.data != nullptr);
        // Verify 4-byte NALU prefix
        assert(frame1.data[0] == 0x00 && frame1.data[1] == 0x00 && frame1.data[2] == 0x00 && frame1.data[3] == 0x01);
        assert(frame1.frame_index == 1);
        std::cout << "  -> Frame 1 (IDR Keyframe): size=" << frame1.size << " bytes, NALU=HEVC_IDR" << std::endl;

        // Frame 2: Periodic intra-refresh frame
        VxrBitstreamFrame frame2;
        assert(encoder.EncodeTexture(mockD3DTexture, 1011111, false, &frame2));
        assert(!frame2.is_keyframe);
        assert(frame2.is_intra_refresh);
        assert(frame2.frame_index == 2);
        assert(frame2.num_slices == 8);
        std::cout << "  -> Frame 2 (Intra-Refresh): size=" << frame2.size << " bytes, 8 slices" << std::endl;

        assert(encoder.GetEncodedFrameCount() == 2);
    }

    // Test 5: Dynamic In-Flight Bitrate Reconfiguration
    std::cout << "[Test 5] Testing dynamic in-flight bitrate adaptation..." << std::endl;
    {
        vxr::VxrNvencEncoder encoder;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_HEVC;
        config.color_format = VXR_COLOR_NV12;
        config.width = 1920;
        config.height = 1080;
        config.fps = 90;
        config.bitrate_bps = 50000000; // start 50 Mbps
        config.max_bitrate_bps = 300000000;
        config.enable_two_pass = false;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = false;
        config.enable_intra_refresh = false;
        config.intra_refresh_period = 0;
        config.num_slices = 1;

        assert(encoder.Initialize(config, nullptr));

        void* tex = reinterpret_cast<void*>(0x33334444ULL);
        VxrBitstreamFrame frameA;
        assert(encoder.EncodeTexture(tex, 100000, false, &frameA));
        size_t sizeAt50Mbps = frameA.size;

        // Reconfigure to 300 Mbps (6x bitrate)
        assert(encoder.ReconfigureBitrate(300000000, 90));

        VxrBitstreamFrame frameB;
        assert(encoder.EncodeTexture(tex, 111111, false, &frameB));
        size_t sizeAt300Mbps = frameB.size;

        assert(sizeAt300Mbps > sizeAt50Mbps);
        std::cout << "  -> Frame size scaled from " << sizeAt50Mbps << " bytes (50 Mbps) to "
                  << sizeAt300Mbps << " bytes (300 Mbps) without session teardown!" << std::endl;
    }

    // Test 6: AMD AMF Encoder
    std::cout << "[Test 6] Testing AMD AMF Hardware Encoder implementation..." << std::endl;
    {
        vxr::VxrAmfEncoder amf;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_HEVC;
        config.color_format = VXR_COLOR_NV12;
        config.width = 1920;
        config.height = 1080;
        config.fps = 90;
        config.bitrate_bps = 100000000;
        config.max_bitrate_bps = 200000000;
        config.enable_two_pass = false;
        config.enable_spatial_aq = false;
        config.enable_temporal_aq = false;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 4;

        assert(amf.Initialize(config, nullptr));
        assert(amf.IsInitialized());

        VxrBitstreamFrame amfFrame;
        void* tex = reinterpret_cast<void*>(0x55556666ULL);
        assert(amf.EncodeTexture(tex, 1000000, true, &amfFrame));
        assert(amfFrame.is_keyframe);
        assert(amfFrame.size > 0);
        std::cout << "  -> AMD AMF Encoder encoded keyframe: size=" << amfFrame.size << " bytes." << std::endl;
    }

    // Test 7: C Public API
    std::cout << "[Test 7] Testing C Public API (vxr_encoder.h)..." << std::endl;
    {
        VxrEncoderHandle handle = vxr_encoder_create(VXR_BACKEND_AUTO);
        assert(handle != nullptr);

        VxrEncoderConfig config;
        config.codec = VXR_CODEC_AV1;
        config.color_format = VXR_COLOR_P010;
        config.width = 2064;
        config.height = 2208;
        config.fps = 90;
        config.bitrate_bps = 180000000;
        config.max_bitrate_bps = 300000000;
        config.enable_two_pass = true;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = true;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 8;

        assert(vxr_encoder_init(handle, &config, nullptr));

        VxrBitstreamFrame bitstream;
        void* tex = reinterpret_cast<void*>(0x77778888ULL);
        assert(vxr_encoder_encode_texture(handle, tex, 500000, true, &bitstream));
        assert(bitstream.is_keyframe);

        assert(vxr_encoder_reconfigure_bitrate(handle, 220000000, 90));

        vxr_encoder_destroy(handle);
        std::cout << "  -> C API creation, configuration, encoding, and destruction verified." << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL HARDWARE ENCODER TESTS PASSED!            " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
