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

struct VulkanFrameRenderState {
    bool in_frame;
    uint32_t eye_draw_calls;
    void* bound_eye_textures[2];
    bool cursor_quad_drawn;
    float cursor_x;
    float cursor_y;
    float cursor_size_x;
    float cursor_size_y;
    bool menu_quad_drawn;
    bool perf_hud_quad_drawn;
    uint32_t active_layer_count;
};

/// Vulkan 1.3 Renderer implementing zero-copy AHardwareBuffer texture binding & VR Overlay Compositing
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

    /// Uploads RGBA8888 pixels for the interactive 3D VR Menu Overlay quad texture
    bool UpdateMenuOverlayTexture(const uint32_t* rgbaPixels, uint32_t width, uint32_t height);

    /// Uploads RGBA8888 pixels for the floating Performance HUD Overlay quad texture
    bool UpdatePerformanceHudTexture(const uint32_t* rgbaPixels, uint32_t width, uint32_t height);

    /// Sets the virtual environment preset, virtual desktop distance (m), and curvature (0..1)
    void SetEnvironmentPreset(uint32_t envPresetId, float screenDistanceM, float screenCurvature);

    /// Begins rendering a frame
    bool BeginFrame();

    /// Draws the video stream quad into the eye render target
    void RenderStreamQuad(const VulkanTexture& texture, int eye_index);

    /// Draws the 0ms hardware cursor overlay quad
    void RenderCursorQuad(float cursor_x, float cursor_y, float size_x, float size_y, bool visible);

    /// Draws the VR Menu Overlay Quad and/or Performance HUD Quad into the compositor command list
    void RenderMenuOverlayQuad(bool menuVisible, bool perfHudVisible);

    /// Submits and ends the frame
    bool EndFrame();

    bool IsInitialized() const { return m_initialized; }
    bool IsZeroCopySupported() const { return m_zeroCopySupported; }
    uint32_t GetWidth() const { return m_config.width; }
    uint32_t GetHeight() const { return m_config.height; }
    uint64_t GetRenderedFrameCount() const { return m_renderedFrameCount; }

    const VulkanTexture& GetMenuOverlayTexture() const { return m_menuOverlayTex; }
    const VulkanTexture& GetPerformanceHudTexture() const { return m_perfHudTex; }
    uint32_t GetEnvironmentPresetId() const { return m_envPresetId; }
    float GetScreenDistanceM() const { return m_screenDistanceM; }
    float GetScreenCurvature() const { return m_screenCurvature; }
    const VulkanFrameRenderState& GetLastFrameState() const { return m_lastFrameState; }
    uint32_t GetActiveCompositionLayerCount() const { return m_lastFrameState.active_layer_count; }

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();

    VulkanRendererConfig m_config;
    bool m_initialized;
    bool m_zeroCopySupported;
    uint64_t m_renderedFrameCount;

    bool m_realVkInstance;
    bool m_realVkDevice;
    bool m_realVkCommandPool;
    uint32_t m_graphicsQueueFamilyIndex;

    void* m_instance;            // VkInstance
    void* m_physicalDevice;      // VkPhysicalDevice
    void* m_device;              // VkDevice
    void* m_graphicsQueue;       // VkQueue
    void* m_commandPool;         // VkCommandPool
    void* m_renderPass;          // VkRenderPass

    VulkanTexture m_menuOverlayTex;
    std::vector<uint32_t> m_menuOverlayCpuStaging;

    VulkanTexture m_perfHudTex;
    std::vector<uint32_t> m_perfHudCpuStaging;

    uint32_t m_envPresetId;
    float m_screenDistanceM;
    float m_screenCurvature;

    VulkanFrameRenderState m_currentFrameState;
    VulkanFrameRenderState m_lastFrameState;
};

} // namespace vxr

#endif // VXR_VULKAN_RENDERER_H
