#ifndef VXR_VULKAN_RENDERER_H
#define VXR_VULKAN_RENDERER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>

namespace vxr {

struct VulkanTexture {
    void* image;                 // VkImage
    void* memory;                // VkDeviceMemory
    void* image_view;            // VkImageView
    void* sampler;               // VkSampler
    uint32_t width;
    uint32_t height;
    uint32_t format;             // VkFormat
    bool is_external_ycbcr;
    void* native_hardware_buffer;// AHardwareBuffer*
};

struct VulkanRendererConfig {
    uint32_t width;
    uint32_t height;
    bool enable_validation_layers;
    bool enable_zero_copy_ahb;
    uint32_t msaa_samples;
};

/// Vulkan 1.3 Renderer implementing zero-copy AHardwareBuffer texture binding
class VxrVulkanRenderer {
public:
    VxrVulkanRenderer();
    ~VxrVulkanRenderer();

    bool Initialize(const VulkanRendererConfig& config);
    void Cleanup();

    /// Zero-Copy: Imports an AHardwareBuffer directly into a Vulkan VkImage without copying through CPU
    bool ImportHardwareBuffer(void* ahardware_buffer, uint32_t width, uint32_t height, VulkanTexture* out_texture);

    /// Releases imported hardware buffer texture resources
    void ReleaseTexture(VulkanTexture* texture);

    /// Begins rendering a frame
    bool BeginFrame();

    /// Draws the video stream quad into the eye render target
    void RenderStreamQuad(const VulkanTexture& texture, int eye_index);

    /// Draws the 0ms hardware cursor overlay quad
    void RenderCursorQuad(float cursor_x, float cursor_y, float size_x, float size_y, bool visible);

    /// Submits and ends the frame
    bool EndFrame();

    bool IsInitialized() const { return m_initialized; }
    bool IsZeroCopySupported() const { return m_zeroCopySupported; }
    uint32_t GetWidth() const { return m_config.width; }
    uint32_t GetHeight() const { return m_config.height; }
    uint64_t GetRenderedFrameCount() const { return m_renderedFrameCount; }

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();

    VulkanRendererConfig m_config;
    bool m_initialized;
    bool m_zeroCopySupported;
    uint64_t m_renderedFrameCount;

    void* m_instance;            // VkInstance
    void* m_physicalDevice;      // VkPhysicalDevice
    void* m_device;              // VkDevice
    void* m_commandPool;         // VkCommandPool
    void* m_renderPass;          // VkRenderPass
};

} // namespace vxr

#endif // VXR_VULKAN_RENDERER_H
