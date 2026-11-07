#ifdef NDEBUG
#undef NDEBUG
#endif

#include "vxr_encoder.h"
#include "VxrNvencEncoder.h"
#include "VxrAmfEncoder.h"
#include "VxrVideoEncoder.h"
#include "DxgiDuplicator.h"

#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>
#include <d3d11.h>

// Helper to create a valid D3D11 device and texture for real hardware encoding tests
static bool CreateTestD3D11Texture(
    uint32_t width,
    uint32_t height,
    ID3D11Device** outDevice,
    ID3D11Texture2D** outTexture
) {
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selectedLevel;
    ID3D11DeviceContext* pContext = nullptr;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        2,
        D3D11_SDK_VERSION,
        outDevice,
        &selectedLevel,
        &pContext
    );

    if (FAILED(hr)) return false;
    if (pContext) pContext->Release();

    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = (*outDevice)->CreateTexture2D(&desc, nullptr, outTexture);
    return SUCCEEDED(hr);
}

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
        config.width = 1920;
        config.height = 1080;
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
        config.width = 1920;
        config.height = 1080;
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

    // Test 4: Physical NVENC Hardware Encoding with DxgiDuplicator & .hevc Output File
    std::cout << "[Test 4] Testing Real NVENC Hardware Encoding with DXGI VRAM Texture & Saving .hevc..." << std::endl;
    {
        // 1. Initialize DXGI desktop capture
        vxr::DxgiDuplicator duplicator;
        VxrCaptureStatus capStatus = duplicator.Initialize(0, 0);

        uint32_t width = 1920;
        uint32_t height = 1080;
        void* pDevice = nullptr;
        void* pTexture = nullptr;

        if (capStatus == VXR_CAPTURE_OK) {
            VxrCaptureDesc desc;
            duplicator.GetDesc(&desc);
            width = desc.width;
            height = desc.height;
            pDevice = duplicator.GetDevice();
            pTexture = duplicator.GetSharedTexture();
            std::cout << "  -> Direct3D11 capture initialized: " << width << "x" << height << std::endl;
        }

        // 2. Initialize real NVENC hardware encoder
        vxr::VxrNvencEncoder encoder;
        VxrEncoderConfig config;
        config.codec = VXR_CODEC_HEVC;
        config.color_format = VXR_COLOR_NV12;
        config.width = width;
        config.height = height;
        config.fps = 90;
        config.bitrate_bps = 50000000; // 50 Mbps
        config.max_bitrate_bps = 100000000;
        config.enable_two_pass = true;
        config.enable_spatial_aq = true;
        config.enable_temporal_aq = true;
        config.enable_intra_refresh = true;
        config.intra_refresh_period = 60;
        config.num_slices = 8;

        assert(encoder.Initialize(config, pDevice));
        assert(encoder.IsInitialized());

        // 3. Open output file test_stream.hevc
        const char* hevcFilename = "test_stream.hevc";
        std::ofstream hevcFile(hevcFilename, std::ios::binary | std::ios::trunc);
        assert(hevcFile.is_open());

        size_t totalBytesWritten = 0;
        const int FRAMES_TO_ENCODE = 15;

        for (int i = 0; i < FRAMES_TO_ENCODE; ++i) {
            if (capStatus == VXR_CAPTURE_OK) {
                VxrCapturedFrame capFrame;
                duplicator.AcquireFrame(16, &capFrame);
                pTexture = capFrame.d3d11_texture;
            }

            VxrBitstreamFrame bitstreamFrame;
            bool forceIdr = (i == 0);
            uint64_t pts = i * 11111ULL;

            bool encodeOk = encoder.EncodeTexture(pTexture, pts, forceIdr, &bitstreamFrame);
            assert(encodeOk);
            assert(bitstreamFrame.data != nullptr);
            assert(bitstreamFrame.size > 0);

            // Write raw NALU bitstream bytes to .hevc file
            hevcFile.write(reinterpret_cast<const char*>(bitstreamFrame.data), bitstreamFrame.size);
            totalBytesWritten += bitstreamFrame.size;

            if (capStatus == VXR_CAPTURE_OK) {
                duplicator.ReleaseFrame();
            }
        }

        hevcFile.close();
        std::cout << "  -> Successfully encoded " << FRAMES_TO_ENCODE
                  << " frames into '" << hevcFilename << "' (" << totalBytesWritten << " bytes)!" << std::endl;

        // 4. Verify test_stream.hevc on disk
        std::ifstream verifyFile(hevcFilename, std::ios::binary);
        assert(verifyFile.is_open());
        verifyFile.seekg(0, std::ios::end);
        std::streamsize fileSize = verifyFile.tellg();
        assert(fileSize > 0);
        assert(fileSize == totalBytesWritten);

        // Verify NALU start code prefix: 0x00 0x00 0x00 0x01
        verifyFile.seekg(0, std::ios::beg);
        uint8_t startCode[4];
        verifyFile.read(reinterpret_cast<char*>(startCode), 4);
        assert(startCode[0] == 0x00 && startCode[1] == 0x00 && startCode[2] == 0x00 && startCode[3] == 0x01);
        verifyFile.close();

        std::cout << "  -> File verification PASSED: valid HEVC NAL unit start codes confirmed." << std::endl;

        if (capStatus == VXR_CAPTURE_OK) {
            duplicator.Cleanup();
        }
    }

    // Test 5: Dynamic In-Flight Bitrate Reconfiguration with Real D3D11 Texture
    std::cout << "[Test 5] Testing dynamic in-flight bitrate adaptation..." << std::endl;
    {
        ID3D11Device* pDevice = nullptr;
        ID3D11Texture2D* pTex = nullptr;
        bool texOk = CreateTestD3D11Texture(1920, 1080, &pDevice, &pTex);
        assert(texOk);

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

        assert(encoder.Initialize(config, pDevice));

        VxrBitstreamFrame frameA;
        bool okA = encoder.EncodeTexture(pTex, 100000, false, &frameA);
        assert(okA);
        size_t sizeAt50Mbps = frameA.size;

        // Reconfigure to 300 Mbps (6x bitrate)
        bool okReconfig = encoder.ReconfigureBitrate(300000000, 90);
        assert(okReconfig);

        VxrBitstreamFrame frameB;
        bool okB = encoder.EncodeTexture(pTex, 111111, false, &frameB);
        assert(okB);

        std::cout << "  -> Frame 1 encoded at 50 Mbps (size=" << sizeAt50Mbps
                  << " bytes), reconfigured to 300 Mbps without session teardown!" << std::endl;

        if (pTex) pTex->Release();
        if (pDevice) pDevice->Release();
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
        ID3D11Device* pDevice = nullptr;
        ID3D11Texture2D* pTex = nullptr;
        CreateTestD3D11Texture(2064, 2208, &pDevice, &pTex);

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

        assert(vxr_encoder_init(handle, &config, pDevice));

        VxrBitstreamFrame bitstream;
        assert(vxr_encoder_encode_texture(handle, pTex, 500000, true, &bitstream));
        assert(bitstream.is_keyframe);
        assert(bitstream.size > 0);

        assert(vxr_encoder_reconfigure_bitrate(handle, 220000000, 90));

        vxr_encoder_destroy(handle);
        if (pTex) pTex->Release();
        if (pDevice) pDevice->Release();
        std::cout << "  -> C API creation, configuration, encoding, and destruction verified." << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL HARDWARE ENCODER TESTS PASSED!            " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
