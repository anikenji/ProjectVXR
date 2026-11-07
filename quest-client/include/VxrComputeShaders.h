#ifndef VXR_COMPUTE_SHADERS_H
#define VXR_COMPUTE_SHADERS_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace vxr {

/// Validates and loads SPIR-V shader bytecode for Vulkan VkShaderModule creation
class VxrSpirvLoader {
public:
    static bool LoadSpirvFromFile(const std::string& filepath, std::vector<uint32_t>& out_words);
    static bool ValidateSpirvHeader(const uint32_t* words, size_t word_count);
};

/// On-Device Synchronous Spacewarp (SSW) Motion Vector Reprojection Pipeline
class VxrSswCompute {
public:
    VxrSswCompute();
    ~VxrSswCompute();

    bool Initialize(const std::string& shader_spv_path, uint32_t width, uint32_t height);

    /// Reprojects dropped frames using 2D screen motion vectors and head rotation delta
    void ReprojectFrame(
        const float* prev_frame_rgba,
        const float* motion_vectors_xy,
        uint32_t width,
        uint32_t height,
        float time_delta,
        float mv_scale,
        float* out_reprojected_rgba
    );

    bool IsInitialized() const { return m_initialized; }
    uint64_t GetReprojectedCount() const { return m_reprojectedCount; }

private:
    bool m_initialized;
    uint32_t m_width;
    uint32_t m_height;
    uint64_t m_reprojectedCount;
    std::vector<uint32_t> m_spirvWords;
};

/// AMD FidelityFX Contrast Adaptive Sharpening (CAS) Pipeline
class VxrCasCompute {
public:
    VxrCasCompute();
    ~VxrCasCompute();

    bool Initialize(const std::string& shader_spv_path, uint32_t width, uint32_t height);

    /// Applies contrast-adaptive sharpening to remove video compression blur
    void SharpenFrame(
        const float* input_rgba,
        uint32_t width,
        uint32_t height,
        float sharpness,
        float* out_sharpened_rgba
    );

    bool IsInitialized() const { return m_initialized; }
    uint64_t GetSharpenedCount() const { return m_sharpenedCount; }

private:
    bool m_initialized;
    uint32_t m_width;
    uint32_t m_height;
    uint64_t m_sharpenedCount;
    std::vector<uint32_t> m_spirvWords;
};

/// Qualcomm Snapdragon Game Super Resolution (SGSR) Pipeline
class VxrSgsrCompute {
public:
    VxrSgsrCompute();
    ~VxrSgsrCompute();

    bool Initialize(const std::string& shader_spv_path, uint32_t width, uint32_t height);

    /// Applies directional edge-preserving spatial sharpening optimized for Adreno GPU
    void ProcessFrame(
        const float* input_rgba,
        uint32_t width,
        uint32_t height,
        float edge_threshold,
        float edge_sharpness,
        float* out_enhanced_rgba
    );

    bool IsInitialized() const { return m_initialized; }
    uint64_t GetProcessedCount() const { return m_processedCount; }

private:
    bool m_initialized;
    uint32_t m_width;
    uint32_t m_height;
    uint64_t m_processedCount;
    std::vector<uint32_t> m_spirvWords;
};

} // namespace vxr

#endif // VXR_COMPUTE_SHADERS_H
