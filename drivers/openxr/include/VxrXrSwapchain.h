#ifndef VXR_XR_SWAPCHAIN_H
#define VXR_XR_SWAPCHAIN_H

#include "VxrXrNegotiation.h"
#include <vector>
#include <mutex>

namespace vxr {

struct SwapchainTextureItem {
    void* d3d11_texture;      // ID3D11Texture2D*
    HANDLE shared_handle;     // Windows NT shared handle
    bool is_acquired;
};

class VxrXrSwapchain {
public:
    VxrXrSwapchain(const XrSwapchainCreateInfo& createInfo);
    ~VxrXrSwapchain();

    XrResult EnumerateImages(uint32_t imageCapacityInput, uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images);
    XrResult AcquireImage(const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index);
    XrResult WaitImage(const XrSwapchainImageWaitInfo* waitInfo);
    XrResult ReleaseImage(const XrSwapchainImageReleaseInfo* releaseInfo);

    uint32_t GetWidth() const { return m_createInfo.width; }
    uint32_t GetHeight() const { return m_createInfo.height; }
    int64_t GetFormat() const { return m_createInfo.format; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_images.size()); }
    HANDLE GetSharedHandle(uint32_t index) const;
    void* GetTexture(uint32_t index) const;

private:
    void AllocateImages();

    XrSwapchainCreateInfo m_createInfo;
    std::vector<SwapchainTextureItem> m_images;
    uint32_t m_currentAcquiredIndex;
    mutable std::mutex m_mutex;
};

} // namespace vxr

#endif // VXR_XR_SWAPCHAIN_H
