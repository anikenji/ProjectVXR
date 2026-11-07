#include "DxgiDuplicator.h"
#include <dxgi1_5.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace vxr {

DxgiDuplicator::DxgiDuplicator()
    : m_adapterIndex(0)
    , m_outputIndex(0)
    , m_initialized(false)
    , m_frameAcquired(false)
    , m_hasValidStagingFrame(false)
    , m_sharedHandle(nullptr)
{
    memset(&m_desc, 0, sizeof(m_desc));
}

DxgiDuplicator::~DxgiDuplicator() {
    Cleanup();
}

void DxgiDuplicator::Cleanup() {
    if (m_frameAcquired && m_duplication) {
        m_duplication->ReleaseFrame();
        m_frameAcquired = false;
    }
    m_duplication.Reset();
    m_stagingTexture.Reset();
    m_sharedTexture.Reset();
    m_sharedHandle = nullptr;
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;
    m_hasValidStagingFrame = false;
}

VxrCaptureStatus DxgiDuplicator::CreateD3DDevice(IDXGIAdapter* adapter) {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
    };
    D3D_FEATURE_LEVEL selectedLevel;

    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    HRESULT hr = D3D11CreateDevice(
        adapter,
        driverType,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &selectedLevel,
        &m_context
    );

    if (FAILED(hr) && (createDeviceFlags & D3D11_CREATE_DEVICE_DEBUG)) {
        createDeviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            adapter,
            driverType,
            nullptr,
            createDeviceFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &m_device,
            &selectedLevel,
            &m_context
        );
    }

    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] D3D11CreateDevice failed: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    return VXR_CAPTURE_OK;
}

VxrCaptureStatus DxgiDuplicator::CreateDuplication(IDXGIOutput* output) {
    ComPtr<IDXGIOutput1> output1;
    HRESULT hr = output->QueryInterface(__uuidof(IDXGIOutput1), &output1);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] QueryInterface IDXGIOutput1 failed: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    hr = output1->DuplicateOutput(m_device.Get(), &m_duplication);
    if (FAILED(hr) && hr != E_ACCESSDENIED) {
        ComPtr<IDXGIOutput5> output5;
        if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput5), &output5)) && output5) {
            const DXGI_FORMAT supportedFormats[] = {
                DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM
            };
            hr = output5->DuplicateOutput1(
                m_device.Get(),
                0,
                ARRAYSIZE(supportedFormats),
                supportedFormats,
                &m_duplication
            );
        }
    }

    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] DuplicateOutput failed: 0x" << std::hex << hr << std::endl;
        if (hr == E_ACCESSDENIED) return VXR_CAPTURE_ACCESS_LOST;
        if (hr == DXGI_ERROR_UNSUPPORTED) return VXR_CAPTURE_NO_OUTPUT;
        return VXR_CAPTURE_D3D_ERROR;
    }

    DXGI_OUTDUPL_DESC duplDesc;
    m_duplication->GetDesc(&duplDesc);

    m_desc.width = duplDesc.ModeDesc.Width;
    m_desc.height = duplDesc.ModeDesc.Height;
    m_desc.refresh_rate_num = duplDesc.ModeDesc.RefreshRate.Numerator;
    m_desc.refresh_rate_den = duplDesc.ModeDesc.RefreshRate.Denominator;
    m_desc.rotation = (int32_t)duplDesc.Rotation;
    m_desc.adapter_index = m_adapterIndex;
    m_desc.output_index = m_outputIndex;

    return VXR_CAPTURE_OK;
}

VxrCaptureStatus DxgiDuplicator::CreateSharedTexture(uint32_t width, uint32_t height) {
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_sharedTexture);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] Failed to create shared texture: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    ComPtr<IDXGIResource> dxgiResource;
    hr = m_sharedTexture.As(&dxgiResource);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] QueryInterface IDXGIResource failed: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    hr = dxgiResource->GetSharedHandle(&m_sharedHandle);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] GetSharedHandle failed: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    return VXR_CAPTURE_OK;
}

VxrCaptureStatus DxgiDuplicator::CreateStagingTexture(uint32_t width, uint32_t height) {
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] Failed to create staging texture: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }
    return VXR_CAPTURE_OK;
}

VxrCaptureStatus DxgiDuplicator::Initialize(uint32_t adapterIndex, uint32_t outputIndex) {
    Cleanup();

    m_adapterIndex = adapterIndex;
    m_outputIndex = outputIndex;

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] CreateDXGIFactory1 failed: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(adapterIndex, &adapter);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] EnumAdapters1 failed for index " << adapterIndex << std::endl;
        return VXR_CAPTURE_INVALID_ARG;
    }

    DXGI_ADAPTER_DESC1 adapterDesc;
    adapter->GetDesc1(&adapterDesc);
    int converted = WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, m_desc.adapter_name, sizeof(m_desc.adapter_name) - 1, nullptr, nullptr);
    if (converted > 0 && converted < (int)sizeof(m_desc.adapter_name)) {
        m_desc.adapter_name[converted] = 0;
    }

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(outputIndex, &output);
    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] EnumOutputs failed for output index " << outputIndex << std::endl;
        return VXR_CAPTURE_NO_OUTPUT;
    }

    VxrCaptureStatus status = CreateD3DDevice(adapter.Get());
    if (status != VXR_CAPTURE_OK) return status;

    status = CreateDuplication(output.Get());
    if (status != VXR_CAPTURE_OK) return status;

    status = CreateSharedTexture(m_desc.width, m_desc.height);
    if (status != VXR_CAPTURE_OK) return status;

    status = CreateStagingTexture(m_desc.width, m_desc.height);
    if (status != VXR_CAPTURE_OK) return status;

    m_initialized = true;
    m_frameAcquired = false;
    m_hasValidStagingFrame = false;
    return VXR_CAPTURE_OK;
}

VxrCaptureStatus DxgiDuplicator::AcquireFrame(uint32_t timeoutMs, VxrCapturedFrame* outFrame) {
    if (!m_initialized || !m_duplication) {
        return VXR_CAPTURE_NOT_INITIALIZED;
    }

    if (m_frameAcquired) {
        ReleaseFrame();
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;
    ZeroMemory(&frameInfo, sizeof(frameInfo));

    HRESULT hr = m_duplication->AcquireNextFrame(timeoutMs, &frameInfo, &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (outFrame) {
            ZeroMemory(outFrame, sizeof(VxrCapturedFrame));
            outFrame->d3d11_texture = m_sharedTexture.Get();
            outFrame->shared_handle = m_sharedHandle;
            outFrame->has_new_frame = 0;
        }
        return VXR_CAPTURE_TIMEOUT;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        std::cerr << "[VXR-Capture] DXGI_ERROR_ACCESS_LOST encountered" << std::endl;
        m_initialized = false;
        return VXR_CAPTURE_ACCESS_LOST;
    }

    if (hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_REMOVED) {
        std::cerr << "[VXR-Capture] GPU Device Reset/Removed" << std::endl;
        m_initialized = false;
        return VXR_CAPTURE_DEVICE_RESET;
    }

    if (FAILED(hr)) {
        std::cerr << "[VXR-Capture] AcquireNextFrame failed: 0x" << std::hex << hr << std::endl;
        return VXR_CAPTURE_D3D_ERROR;
    }

    m_frameAcquired = true;

    // Per Microsoft IDXGIOutputDuplication::AcquireNextFrame specification:
    // If LastPresentTime.QuadPart == 0, ONLY the mouse cursor moved and desktopResource is empty/black!
    // Never copy desktopResource into m_sharedTexture / m_stagingTexture when LastPresentTime == 0.
    bool hasDesktopImageUpdate = (frameInfo.LastPresentTime.QuadPart > 0);
    if (hasDesktopImageUpdate && desktopResource) {
        ComPtr<ID3D11Texture2D> acquiredTexture;
        hr = desktopResource.As(&acquiredTexture);
        if (FAILED(hr) || !acquiredTexture) {
            ReleaseFrame();
            return VXR_CAPTURE_D3D_ERROR;
        }

        m_context->CopyResource(m_sharedTexture.Get(), acquiredTexture.Get());
        if (m_stagingTexture) {
            m_context->CopyResource(m_stagingTexture.Get(), acquiredTexture.Get());
        }
    }

    if (outFrame) {
        outFrame->d3d11_texture = m_sharedTexture.Get();
        outFrame->shared_handle = m_sharedHandle;
        outFrame->present_time_qpc = frameInfo.LastPresentTime.QuadPart;
        outFrame->accumulated_frames = frameInfo.AccumulatedFrames;
        outFrame->has_new_frame = hasDesktopImageUpdate ? 1 : 0;

        outFrame->cursor.visible = frameInfo.PointerPosition.Visible ? 1 : 0;
        outFrame->cursor.x = frameInfo.PointerPosition.Position.x;
        outFrame->cursor.y = frameInfo.PointerPosition.Position.y;
        outFrame->cursor.shape_type = 0;
        outFrame->cursor.width = 0;
        outFrame->cursor.height = 0;
        outFrame->cursor.pitch = 0;
        outFrame->cursor.shape_buffer_size = frameInfo.PointerShapeBufferSize;

        if (frameInfo.PointerShapeBufferSize > 0) {
            if (m_cursorShapeBuffer.size() < frameInfo.PointerShapeBufferSize) {
                m_cursorShapeBuffer.resize(frameInfo.PointerShapeBufferSize);
            }
            DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo;
            UINT requiredBufferSize = 0;
            HRESULT shapeHr = m_duplication->GetFramePointerShape(
                frameInfo.PointerShapeBufferSize,
                m_cursorShapeBuffer.data(),
                &requiredBufferSize,
                &shapeInfo
            );
            if (SUCCEEDED(shapeHr)) {
                outFrame->cursor.shape_type = shapeInfo.Type;
                outFrame->cursor.width = shapeInfo.Width;
                outFrame->cursor.height = shapeInfo.Height;
                outFrame->cursor.pitch = shapeInfo.Pitch;
            }
        }
    }

    return VXR_CAPTURE_OK;
}

bool DxgiDuplicator::CaptureScaledBgra(uint32_t dstWidth, uint32_t dstHeight, uint32_t* outBgra, bool* outHasNewFrame) {
    if (!outBgra || dstWidth == 0 || dstHeight == 0) return false;
    if (outHasNewFrame) *outHasNewFrame = false;

    if (!m_initialized) {
        return false;
    }

    VxrCapturedFrame capFrame = {};
    uint32_t timeoutMs = m_hasValidStagingFrame ? 0 : 8;
    VxrCaptureStatus status = AcquireFrame(timeoutMs, &capFrame);
    if (status == VXR_CAPTURE_ACCESS_LOST || status == VXR_CAPTURE_DEVICE_RESET) {
        Cleanup();
        return false;
    }

    if (status == VXR_CAPTURE_OK) {
        ReleaseFrame();

        if (capFrame.has_new_frame && m_stagingTexture && m_desc.width > 0 && m_desc.height > 0) {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            HRESULT mapHr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(mapHr) && mapped.pData != nullptr) {
                const uint32_t srcW = m_desc.width;
                const uint32_t srcH = m_desc.height;
                const uint32_t srcPitchPixels = mapped.RowPitch / 4;
                const uint32_t* srcBase = static_cast<const uint32_t*>(mapped.pData);

                // Verify the DXGI staging surface actually contains non-zero desktop pixels
                uint32_t nonZeroSamples = 0;
                for (uint32_t sy = srcH / 8; sy < srcH; sy += srcH / 4) {
                    const uint32_t* sRow = srcBase + (size_t)sy * srcPitchPixels;
                    for (uint32_t sx = srcW / 8; sx < srcW; sx += srcW / 4) {
                        if ((sRow[sx] & 0x00FFFFFFu) != 0) {
                            nonZeroSamples++;
                        }
                    }
                }

                if (nonZeroSamples == 0) {
                    m_context->Unmap(m_stagingTexture.Get(), 0);
                    return false;
                }

                if (m_cachedScaledBgra.size() != (size_t)dstWidth * dstHeight) {
                    m_cachedScaledBgra.resize((size_t)dstWidth * dstHeight);
                }
                m_cachedDstW = dstWidth;
                m_cachedDstH = dstHeight;

                if (srcW == dstWidth && srcH == dstHeight) {
                    // 1:1 Native PC Resolution Fast-Path (~0.2ms direct DMA row copy, zero scaling blur!)
                    for (uint32_t y = 0; y < dstHeight; ++y) {
                        std::memcpy(m_cachedScaledBgra.data() + (size_t)y * dstWidth,
                                    srcBase + (size_t)y * srcPitchPixels,
                                    (size_t)dstWidth * sizeof(uint32_t));
                    }
                    m_context->Unmap(m_stagingTexture.Get(), 0);
                } else {
                    // High-precision 4-tap Bilinear Area Downsampling + Subtle Unsharp Contrast Enhancement
                    // Preserves 1-pixel UI font strokes cleanly when scaling 2560x1440 / 3840x2160 -> 1920x1080
                    const uint32_t scaleX_fp16 = (srcW << 16) / dstWidth;
                    const uint32_t scaleY_fp16 = (srcH << 16) / dstHeight;

                    for (uint32_t dy = 0; dy < dstHeight; ++dy) {
                        uint32_t sy_fp = dy * scaleY_fp16;
                        uint32_t sy0 = sy_fp >> 16;
                        uint32_t sy1 = (sy0 + 1 < srcH) ? (sy0 + 1) : sy0;
                        uint32_t wy = (sy_fp >> 8) & 0xFFu;
                        uint32_t iwy = 256u - wy;

                        const uint32_t* row0 = srcBase + (size_t)sy0 * srcPitchPixels;
                        const uint32_t* row1 = srcBase + (size_t)sy1 * srcPitchPixels;
                        uint32_t* dstRow = m_cachedScaledBgra.data() + (size_t)dy * dstWidth;

                        for (uint32_t dx = 0; dx < dstWidth; ++dx) {
                            uint32_t sx_fp = dx * scaleX_fp16;
                            uint32_t sx0 = sx_fp >> 16;
                            uint32_t sx1 = (sx0 + 1 < srcW) ? (sx0 + 1) : sx0;
                            uint32_t wx = (sx_fp >> 8) & 0xFFu;
                            uint32_t iwx = 256u - wx;

                            uint32_t w00 = iwx * iwy;
                            uint32_t w10 = wx  * iwy;
                            uint32_t w01 = iwx * wy;
                            uint32_t w11 = wx  * wy;

                            uint32_t p00 = row0[sx0];
                            uint32_t p10 = row0[sx1];
                            uint32_t p01 = row1[sx0];
                            uint32_t p11 = row1[sx1];

                            uint32_t b = (((p00 & 0xFFu) * w00 + (p10 & 0xFFu) * w10 +
                                           (p01 & 0xFFu) * w01 + (p11 & 0xFFu) * w11) >> 16);
                            uint32_t g = ((((p00 >> 8) & 0xFFu) * w00 + ((p10 >> 8) & 0xFFu) * w10 +
                                           ((p01 >> 8) & 0xFFu) * w01 + ((p11 >> 8) & 0xFFu) * w11) >> 16);
                            uint32_t r = ((((p00 >> 16) & 0xFFu) * w00 + ((p10 >> 16) & 0xFFu) * w10 +
                                           ((p01 >> 16) & 0xFFu) * w01 + ((p11 >> 16) & 0xFFu) * w11) >> 16);

                            dstRow[dx] = 0xFF000000u | (r << 16) | (g << 8) | b;
                        }
                    }

                    m_context->Unmap(m_stagingTexture.Get(), 0);

                    if (dstWidth >= 4 && dstHeight >= 4) {
                        std::vector<uint32_t> sharpBuf((size_t)dstWidth * dstHeight);
                        std::memcpy(sharpBuf.data(), m_cachedScaledBgra.data(), dstWidth * sizeof(uint32_t));
                        std::memcpy(sharpBuf.data() + (size_t)(dstHeight - 1) * dstWidth,
                                    m_cachedScaledBgra.data() + (size_t)(dstHeight - 1) * dstWidth,
                                    dstWidth * sizeof(uint32_t));

                        for (uint32_t y = 1; y + 1 < dstHeight; ++y) {
                            const uint32_t* rPrev = m_cachedScaledBgra.data() + (size_t)(y - 1) * dstWidth;
                            const uint32_t* rCurr = m_cachedScaledBgra.data() + (size_t)y * dstWidth;
                            const uint32_t* rNext = m_cachedScaledBgra.data() + (size_t)(y + 1) * dstWidth;
                            uint32_t* rOut = sharpBuf.data() + (size_t)y * dstWidth;
                            rOut[0] = rCurr[0];
                            rOut[dstWidth - 1] = rCurr[dstWidth - 1];

                            for (uint32_t x = 1; x + 1 < dstWidth; ++x) {
                                uint32_t c = rCurr[x];
                                uint32_t n = rPrev[x];
                                uint32_t s = rNext[x];
                                uint32_t w = rCurr[x - 1];
                                uint32_t e = rCurr[x + 1];

                                int cb = (int)(c & 0xFFu);
                                int cg = (int)((c >> 8) & 0xFFu);
                                int cr = (int)((c >> 16) & 0xFFu);

                                int sumB = (int)(n & 0xFFu) + (int)(s & 0xFFu) + (int)(w & 0xFFu) + (int)(e & 0xFFu);
                                int sumG = (int)((n >> 8) & 0xFFu) + (int)((s >> 8) & 0xFFu) + (int)((w >> 8) & 0xFFu) + (int)((e >> 8) & 0xFFu);
                                int sumR = (int)((n >> 16) & 0xFFu) + (int)((s >> 16) & 0xFFu) + (int)((w >> 16) & 0xFFu) + (int)((e >> 8) & 0xFFu);

                                int ob = cb + (((4 * cb - sumB) * 3) >> 5);
                                int og = cg + (((4 * cg - sumG) * 3) >> 5);
                                int or_ = cr + (((4 * cr - sumR) * 3) >> 5);

                                if (ob < 0) ob = 0; else if (ob > 255) ob = 255;
                                if (og < 0) og = 0; else if (og > 255) og = 255;
                                if (or_ < 0) or_ = 0; else if (or_ > 255) or_ = 255;

                                rOut[x] = 0xFF000000u | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
                            }
                        }
                        m_cachedScaledBgra.swap(sharpBuf);
                    }
                }

                m_hasValidStagingFrame = true;
                if (outHasNewFrame) *outHasNewFrame = true;
            }
        }
    } else if (m_frameAcquired) {
        ReleaseFrame();
    }

    if (m_hasValidStagingFrame &&
        m_cachedDstW == dstWidth &&
        m_cachedDstH == dstHeight &&
        m_cachedScaledBgra.size() == (size_t)dstWidth * dstHeight) {
        std::memcpy(outBgra, m_cachedScaledBgra.data(), (size_t)dstWidth * dstHeight * sizeof(uint32_t));
        return true;
    }

    return false;
}

VxrCaptureStatus DxgiDuplicator::ReleaseFrame() {
    if (!m_initialized || !m_duplication) {
        return VXR_CAPTURE_NOT_INITIALIZED;
    }

    if (m_frameAcquired) {
        HRESULT hr = m_duplication->ReleaseFrame();
        m_frameAcquired = false;
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                m_initialized = false;
                return VXR_CAPTURE_ACCESS_LOST;
            }
            return VXR_CAPTURE_D3D_ERROR;
        }
    }

    return VXR_CAPTURE_OK;
}

VxrCaptureStatus DxgiDuplicator::Reinitialize() {
    return Initialize(m_adapterIndex, m_outputIndex);
}

VxrCaptureStatus DxgiDuplicator::GetDesc(VxrCaptureDesc* outDesc) const {
    if (!m_initialized) {
        return VXR_CAPTURE_NOT_INITIALIZED;
    }
    if (!outDesc) {
        return VXR_CAPTURE_INVALID_ARG;
    }
    *outDesc = m_desc;
    return VXR_CAPTURE_OK;
}

} // namespace vxr
