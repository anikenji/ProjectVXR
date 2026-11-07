#ifndef VXR_HARDWARE_CURSOR_H
#define VXR_HARDWARE_CURSOR_H

#include <stdint.h>
#include <mutex>

namespace vxr {

struct CursorQuadVertex {
    float pos[3];   // 3D position in VR space (x, y, z)
    float uv[2];    // Texture UV (u, v)
};

enum class CursorShapeType {
    DefaultArrow = 0,
    Hand = 1,
    IBeam = 2,
    Crosshair = 3,
    CustomBitmap = 4,
};

/// 0ms Local Hardware Cursor Plane
/// Renders an overlay quad directly into the VR scene at native headset refresh rate (90Hz/120Hz),
/// completely bypassing video frame transport latency for instant responsive mouse interaction.
class VxrHardwareCursor {
public:
    VxrHardwareCursor();
    ~VxrHardwareCursor();

    /// Updates normalized screen coordinates (x, y in [0.0, 1.0]) and visibility
    void UpdatePosition(float normalized_x, float normalized_y, bool visible);

    /// Updates cursor shape type and dimensions
    void UpdateShape(CursorShapeType shape_type, uint32_t width, uint32_t height, const uint8_t* rgba_data);

    /// Computes the 3D VR billboard quad vertices placed directly over the virtual desktop screen
    /// - `desktop_pos`: Center position of virtual desktop in VR space (x, y, z)
    /// - `desktop_width`: Width in meters (e.g. 2.0m)
    /// - `desktop_height`: Height in meters (e.g. 1.125m)
    void CalculateQuadVertices(
        const float desktop_pos[3],
        float desktop_width,
        float desktop_height,
        CursorQuadVertex out_vertices[4]
    );

    float GetNormalizedX() const { return m_normalizedX; }
    float GetNormalizedY() const { return m_normalizedY; }
    bool IsVisible() const { return m_visible; }
    CursorShapeType GetShapeType() const { return m_shapeType; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    uint64_t GetUpdateCount() const { return m_updateCount; }

private:
    mutable std::mutex m_mutex;
    float m_normalizedX;
    float m_normalizedY;
    bool m_visible;
    CursorShapeType m_shapeType;
    uint32_t m_width;
    uint32_t m_height;
    uint64_t m_updateCount;
    float m_cursorSizeMeters;
};

} // namespace vxr

#endif // VXR_HARDWARE_CURSOR_H
