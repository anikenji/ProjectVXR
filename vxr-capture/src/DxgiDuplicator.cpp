#include "DxgiDuplicator.h"
#include <iostream>
#include <sstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace vxr {

DxgiDuplicator::DxgiDuplicator()
    : m_adapterIndex(0)
    , m_outputIndex(0)
    , m_initialized(false)
    , m_frameAcquired(false)
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
    m_sharedTexture.Reset();
    m_sharedHandle = nullptr;
    m_context.Reset();
    m_device.Reset();
    m_initialized = false;
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

    m_initialized = true;
    m_frameAcquired = false;
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

    ComPtr<ID3D11Texture2D> acquiredTexture;
    hr = desktopResource.As(&acquiredTexture);
    if (FAILED(hr)) {
        ReleaseFrame();
        return VXR_CAPTURE_D3D_ERROR;
    }

    m_context->CopyResource(m_sharedTexture.Get(), acquiredTexture.Get());

    if (outFrame) {
        outFrame->d3d11_texture = m_sharedTexture.Get();
        outFrame->shared_handle = m_sharedHandle;
        outFrame->present_time_qpc = frameInfo.LastPresentTime.QuadPart;
        outFrame->accumulated_frames = frameInfo.AccumulatedFrames;
        outFrame->has_new_frame = (frameInfo.AccumulatedFrames > 0) ? 1 : 0;

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
