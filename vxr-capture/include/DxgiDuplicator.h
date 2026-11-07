#ifndef DXGI_DUPLICATOR_H
#define DXGI_DUPLICATOR_H

#include "vxr_capture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

namespace vxr {

class DxgiDuplicator {
public:
    DxgiDuplicator();
    ~DxgiDuplicator();

    // Disallow copy
    DxgiDuplicator(const DxgiDuplicator&) = delete;
    DxgiDuplicator& operator=(const DxgiDuplicator&) = delete;

    VxrCaptureStatus Initialize(uint32_t adapterIndex = 0, uint32_t outputIndex = 0);
    VxrCaptureStatus AcquireFrame(uint32_t timeoutMs, VxrCapturedFrame* outFrame);
    VxrCaptureStatus ReleaseFrame();
    VxrCaptureStatus Reinitialize();
    void Cleanup();

    VxrCaptureStatus GetDesc(VxrCaptureDesc* outDesc) const;
    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    ID3D11Texture2D* GetSharedTexture() const { return m_sharedTexture.Get(); }
    HANDLE GetSharedHandle() const { return m_sharedHandle; }
    bool IsInitialized() const { return m_initialized; }

private:
    VxrCaptureStatus CreateD3DDevice(IDXGIAdapter* adapter);
    VxrCaptureStatus CreateDuplication(IDXGIOutput* output);
    VxrCaptureStatus CreateSharedTexture(uint32_t width, uint32_t height);

    uint32_t m_adapterIndex;
    uint32_t m_outputIndex;
    bool m_initialized;
    bool m_frameAcquired;

    VxrCaptureDesc m_desc;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_sharedTexture;
    HANDLE m_sharedHandle;

    std::vector<uint8_t> m_cursorShapeBuffer;
};

} // namespace vxr

#endif // DXGI_DUPLICATOR_H
