#include "VxrComputeShaders.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace vxr {

// =============================================================================
// VxrSpirvLoader
// =============================================================================

bool VxrSpirvLoader::LoadSpirvFromFile(const std::string& filepath, std::vector<uint32_t>& out_words) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[VXR-SPIRV] Failed to open SPIR-V file: " << filepath << std::endl;
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < 20 || (fileSize % 4) != 0) {
        std::cerr << "[VXR-SPIRV] Invalid SPIR-V file size: " << fileSize << " bytes" << std::endl;
        return false;
    }

    file.seekg(0, std::ios::beg);
    out_words.resize(static_cast<size_t>(fileSize / 4));
    if (!file.read(reinterpret_cast<char*>(out_words.data()), fileSize)) {
        return false;
    }

    return ValidateSpirvHeader(out_words.data(), out_words.size());
}

bool VxrSpirvLoader::ValidateSpirvHeader(const uint32_t* words, size_t word_count) {
    if (words == nullptr || word_count < 5) {
        return false;
    }

    // SPIR-V Header:
    // Word 0: Magic Number (0x07230203)
    if (words[0] != 0x07230203) {
        std::cerr << "[VXR-SPIRV] Bad magic number: 0x" << std::hex << words[0] << std::endl;
        return false;
    }

    // Word 1: Version (e.g. 0x00010300 = SPIR-V 1.3)
    uint32_t major = (words[1] >> 16) & 0xFF;
    uint32_t minor = (words[1] >> 8) & 0xFF;
    if (major < 1) {
        std::cerr << "[VXR-SPIRV] Unsupported SPIR-V version: " << major << "." << minor << std::endl;
        return false;
    }

    return true;
}

// =============================================================================
// VxrSswCompute (Synchronous Spacewarp)
// =============================================================================

VxrSswCompute::VxrSswCompute()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_reprojectedCount(0)
{
}

VxrSswCompute::~VxrSswCompute() {
}

bool VxrSswCompute::Initialize(const std::string& shader_spv_path, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!shader_spv_path.empty()) {
        if (!VxrSpirvLoader::LoadSpirvFromFile(shader_spv_path, m_spirvWords)) {
            std::cerr << "[VXR-SSW] Warning: Could not load SPIR-V from " << shader_spv_path << std::endl;
        }
    }

    m_initialized = true;
    m_reprojectedCount = 0;
    return true;
}

void VxrSswCompute::ReprojectFrame(
    const float* prev_frame_rgba,
    const float* motion_vectors_xy,
    uint32_t width,
    uint32_t height,
    float time_delta,
    float mv_scale,
    float* out_reprojected_rgba
) {
    if (!prev_frame_rgba || !out_reprojected_rgba || width == 0 || height == 0) {
        return;
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t pixel_idx = (y * width + x);
            float mv_x = 0.0f;
            float mv_y = 0.0f;

            if (motion_vectors_xy != nullptr) {
                mv_x = motion_vectors_xy[pixel_idx * 2 + 0] * mv_scale * time_delta;
                mv_y = motion_vectors_xy[pixel_idx * 2 + 1] * mv_scale * time_delta;
            }

            // Backward sample coordinate
            float src_x = static_cast<float>(x) - mv_x;
            float src_y = static_cast<float>(y) - mv_y;

            // Clamp to valid texture boundaries
            int sx = std::clamp(static_cast<int>(std::round(src_x)), 0, static_cast<int>(width - 1));
            int sy = std::clamp(static_cast<int>(std::round(src_y)), 0, static_cast<int>(height - 1));

            size_t src_idx = (sy * width + sx) * 4;
            size_t dst_idx = pixel_idx * 4;

            for (int c = 0; c < 4; ++c) {
                out_reprojected_rgba[dst_idx + c] = prev_frame_rgba[src_idx + c];
            }
        }
    }

    m_reprojectedCount++;
}

// =============================================================================
// VxrCasCompute (AMD FidelityFX CAS)
// =============================================================================

VxrCasCompute::VxrCasCompute()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_sharpenedCount(0)
{
}

VxrCasCompute::~VxrCasCompute() {
}

bool VxrCasCompute::Initialize(const std::string& shader_spv_path, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!shader_spv_path.empty()) {
        if (!VxrSpirvLoader::LoadSpirvFromFile(shader_spv_path, m_spirvWords)) {
            std::cerr << "[VXR-CAS] Warning: Could not load SPIR-V from " << shader_spv_path << std::endl;
        }
    }

    m_initialized = true;
    m_sharpenedCount = 0;
    return true;
}

void VxrCasCompute::SharpenFrame(
    const float* input_rgba,
    uint32_t width,
    uint32_t height,
    float sharpness,
    float* out_sharpened_rgba
) {
    if (!input_rgba || !out_sharpened_rgba || width == 0 || height == 0) {
        return;
    }

    auto get_pixel = [&](int px, int py, int channel) -> float {
        int cx = std::clamp(px, 0, static_cast<int>(width - 1));
        int cy = std::clamp(py, 0, static_cast<int>(height - 1));
        return input_rgba[(cy * width + cx) * 4 + channel];
    };

    for (int y = 0; y < static_cast<int>(height); ++y) {
        for (int x = 0; x < static_cast<int>(width); ++x) {
            size_t dst_idx = (y * width + x) * 4;

            for (int c = 0; c < 3; ++c) {
                float e = get_pixel(x, y, c);
                float a = get_pixel(x, y - 1, c);
                float b = get_pixel(x - 1, y, c);
                float d = get_pixel(x + 1, y, c);
                float f = get_pixel(x, y + 1, c);

                float min_val = std::min({ a, b, d, f, e });
                float max_val = std::max({ a, b, d, f, e });

                // Contrast-adaptive sharpening weight
                float amp = std::clamp(std::min(min_val, 2.0f - max_val) / std::max(max_val, 1e-5f), 0.0f, 1.0f);
                float w = -std::sqrt(amp) * (0.25f * sharpness);

                float filter_sum = (a + b + d + f) * w + e;
                float filter_norm = 4.0f * w + 1.0f;
                float sharpened = std::clamp(filter_sum / filter_norm, min_val, max_val);

                out_sharpened_rgba[dst_idx + c] = sharpened;
            }
            out_sharpened_rgba[dst_idx + 3] = input_rgba[dst_idx + 3]; // preserve alpha
        }
    }

    m_sharpenedCount++;
}

// =============================================================================
// VxrSgsrCompute (Qualcomm SGSR)
// =============================================================================

VxrSgsrCompute::VxrSgsrCompute()
    : m_initialized(false)
    , m_width(0)
    , m_height(0)
    , m_processedCount(0)
{
}

VxrSgsrCompute::~VxrSgsrCompute() {
}

bool VxrSgsrCompute::Initialize(const std::string& shader_spv_path, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    if (!shader_spv_path.empty()) {
        if (!VxrSpirvLoader::LoadSpirvFromFile(shader_spv_path, m_spirvWords)) {
            std::cerr << "[VXR-SGSR] Warning: Could not load SPIR-V from " << shader_spv_path << std::endl;
        }
    }

    m_initialized = true;
    m_processedCount = 0;
    return true;
}

void VxrSgsrCompute::ProcessFrame(
    const float* input_rgba,
    uint32_t width,
    uint32_t height,
    float edge_threshold,
    float edge_sharpness,
    float* out_enhanced_rgba
) {
    if (!input_rgba || !out_enhanced_rgba || width == 0 || height == 0) {
        return;
    }

    auto get_pixel = [&](int px, int py) -> const float* {
        int cx = std::clamp(px, 0, static_cast<int>(width - 1));
        int cy = std::clamp(py, 0, static_cast<int>(height - 1));
        return &input_rgba[(cy * width + cx) * 4];
    };

    auto rgb_to_luma = [](const float* rgb) -> float {
        return 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
    };

    for (int y = 0; y < static_cast<int>(height); ++y) {
        for (int x = 0; x < static_cast<int>(width); ++x) {
            size_t dst_idx = (y * width + x) * 4;

            const float* c = get_pixel(x, y);
            const float* n = get_pixel(x, y - 1);
            const float* s = get_pixel(x, y + 1);
            const float* w = get_pixel(x - 1, y);
            const float* e = get_pixel(x + 1, y);

            const float* nw = get_pixel(x - 1, y - 1);
            const float* ne = get_pixel(x + 1, y - 1);
            const float* sw = get_pixel(x - 1, y + 1);
            const float* se = get_pixel(x + 1, y + 1);

            float luma_nw = rgb_to_luma(nw);
            float luma_ne = rgb_to_luma(ne);
            float luma_sw = rgb_to_luma(sw);
            float luma_se = rgb_to_luma(se);
            float luma_w = rgb_to_luma(w);
            float luma_e = rgb_to_luma(e);
            float luma_n = rgb_to_luma(n);
            float luma_s = rgb_to_luma(s);

            // Sobel edge gradient
            float gx = (luma_ne + 2.0f * luma_e + luma_se) - (luma_nw + 2.0f * luma_w + luma_sw);
            float gy = (luma_sw + 2.0f * luma_s + luma_se) - (luma_nw + 2.0f * luma_n + luma_ne);
            float edge_mag = std::sqrt(gx * gx + gy * gy);

            for (int ch = 0; ch < 3; ++ch) {
                float min_c = std::min({ n[ch], s[ch], w[ch], e[ch], c[ch] });
                float max_c = std::max({ n[ch], s[ch], w[ch], e[ch], c[ch] });

                float enhanced;
                if (edge_mag > edge_threshold) {
                    float blur = (n[ch] + s[ch] + w[ch] + e[ch]) * 0.25f;
                    enhanced = c[ch] + (c[ch] - blur) * (edge_sharpness * 0.5f);
                } else {
                    float blur = (n[ch] + s[ch] + w[ch] + e[ch]) * 0.25f;
                    enhanced = c[ch] + (c[ch] - blur) * (edge_sharpness * 0.25f);
                }

                out_enhanced_rgba[dst_idx + ch] = std::clamp(enhanced, min_c, max_c);
            }
            out_enhanced_rgba[dst_idx + 3] = c[3];
        }
    }

    m_processedCount++;
}

} // namespace vxr
