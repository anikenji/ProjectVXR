#include "VxrHardwareCursor.h"

namespace vxr {

VxrHardwareCursor::VxrHardwareCursor()
    : m_normalizedX(0.5f)
    , m_normalizedY(0.5f)
    , m_visible(true)
    , m_shapeType(CursorShapeType::DefaultArrow)
    , m_width(32)
    , m_height(32)
    , m_updateCount(0)
    , m_cursorSizeMeters(0.035f) // 3.5 cm in VR space
{
}

VxrHardwareCursor::~VxrHardwareCursor() {
}

void VxrHardwareCursor::UpdatePosition(float normalized_x, float normalized_y, bool visible) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_normalizedX = normalized_x;
    m_normalizedY = normalized_y;
    m_visible = visible;
    m_updateCount++;
}

void VxrHardwareCursor::UpdateShape(CursorShapeType shape_type, uint32_t width, uint32_t height, const uint8_t* rgba_data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shapeType = shape_type;
    m_width = width;
    m_height = height;
    m_updateCount++;
}

void VxrHardwareCursor::CalculateQuadVertices(
    const float desktop_pos[3],
    float desktop_width,
    float desktop_height,
    CursorQuadVertex out_vertices[4]
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Compute cursor center in virtual desktop coordinate space
    // Normalized x: [0.0 = left, 1.0 = right] -> [-W/2, +W/2]
    // Normalized y: [0.0 = top,  1.0 = bottom] -> [+H/2, -H/2]
    float offset_x = (m_normalizedX - 0.5f) * desktop_width;
    float offset_y = (0.5f - m_normalizedY) * desktop_height;

    // Offset slightly in front of virtual screen (-Z or +Z depending on forward convention)
    // 2mm offset to prevent Z-fighting against the desktop plane
    float cx = desktop_pos[0] + offset_x;
    float cy = desktop_pos[1] + offset_y;
    float cz = desktop_pos[2] + 0.002f;

    float half_s = m_cursorSizeMeters * 0.5f;

    // 0: Top-Left
    out_vertices[0].pos[0] = cx - half_s;
    out_vertices[0].pos[1] = cy + half_s;
    out_vertices[0].pos[2] = cz;
    out_vertices[0].uv[0] = 0.0f;
    out_vertices[0].uv[1] = 0.0f;

    // 1: Bottom-Left
    out_vertices[1].pos[0] = cx - half_s;
    out_vertices[1].pos[1] = cy - half_s;
    out_vertices[1].pos[2] = cz;
    out_vertices[1].uv[0] = 0.0f;
    out_vertices[1].uv[1] = 1.0f;

    // 2: Bottom-Right
    out_vertices[2].pos[0] = cx + half_s;
    out_vertices[2].pos[1] = cy - half_s;
    out_vertices[2].pos[2] = cz;
    out_vertices[2].uv[0] = 1.0f;
    out_vertices[2].uv[1] = 1.0f;

    // 3: Top-Right
    out_vertices[3].pos[0] = cx + half_s;
    out_vertices[3].pos[1] = cy + half_s;
    out_vertices[3].pos[2] = cz;
    out_vertices[3].uv[0] = 1.0f;
    out_vertices[3].uv[1] = 0.0f;
}

} // namespace vxr
