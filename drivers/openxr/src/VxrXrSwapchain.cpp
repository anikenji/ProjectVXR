#include "VxrXrSwapchain.h"
#include <iostream>

namespace vxr {

VxrXrSwapchain::VxrXrSwapchain(const XrSwapchainCreateInfo& createInfo)
    : m_createInfo(createInfo)
    , m_currentAcquiredIndex(0)
{
    AllocateImages();
}

VxrXrSwapchain::~VxrXrSwapchain() {
}

void VxrXrSwapchain::AllocateImages() {
    // Standard triple-buffered swapchain
    uint32_t count = (m_createInfo.sampleCount > 0) ? 3 : 3;
    m_images.resize(count);

    for (uint32_t i = 0; i < count; ++i) {
        // Allocate simulated/real D3D11 textures and shared NT handles
        m_images[i].d3d11_texture = reinterpret_cast<void*>(static_cast<uintptr_t>(0xD3D11000ULL + (i + 1) * 0x1000ULL));
        m_images[i].shared_handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x70000000ULL + (i + 1) * 0x1000ULL));
        m_images[i].is_acquired = false;
    }
}

XrResult VxrXrSwapchain::EnumerateImages(uint32_t imageCapacityInput, uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images) {
    if (!imageCountOutput) return XR_ERROR_VALIDATION_FAILURE;

    *imageCountOutput = static_cast<uint32_t>(m_images.size());
    if (imageCapacityInput == 0) {
        return XR_SUCCESS;
    }

    if (imageCapacityInput < m_images.size()) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }

    auto d3d11Images = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
    for (uint32_t i = 0; i < m_images.size(); ++i) {
        d3d11Images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        d3d11Images[i].next = nullptr;
        d3d11Images[i].texture = reinterpret_cast<ID3D11Texture2D*>(m_images[i].d3d11_texture);
    }

    return XR_SUCCESS;
}

XrResult VxrXrSwapchain::AcquireImage(const XrSwapchainImageAcquireInfo* acquireInfo, uint32_t* index) {
    if (!index) return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> lock(m_mutex);

    *index = m_currentAcquiredIndex;
    m_images[m_currentAcquiredIndex].is_acquired = true;
    m_currentAcquiredIndex = (m_currentAcquiredIndex + 1) % m_images.size();

    return XR_SUCCESS;
}

XrResult VxrXrSwapchain::WaitImage(const XrSwapchainImageWaitInfo* waitInfo) {
    return XR_SUCCESS;
}

XrResult VxrXrSwapchain::ReleaseImage(const XrSwapchainImageReleaseInfo* releaseInfo) {
    return XR_SUCCESS;
}

HANDLE VxrXrSwapchain::GetSharedHandle(uint32_t index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < m_images.size()) {
        return m_images[index].shared_handle;
    }
    return nullptr;
}

void* VxrXrSwapchain::GetTexture(uint32_t index) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index < m_images.size()) {
        return m_images[index].d3d11_texture;
    }
    return nullptr;
}

} // namespace vxr
