#ifndef VXR_SPATIAL_CANVAS_H
#define VXR_SPATIAL_CANVAS_H

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace vxr {

/// 3D Spatial Panel representing a floating application window in VR
struct SpatialPanel {
    uint32_t panel_id;
    std::string title;            // e.g. "Code Editor", "Web Browser", "Discord"
    float position[3];           // 3D position in meters (x, y, z)
    float orientation[4];        // Rotation quaternion (x, y, z, w)
    float size[2];               // Width and height in meters (e.g. 1.2m x 0.8m)
    float curvature_radius;      // 0.0 = flat plane, > 0.0 = cylindrical curve radius
    void* swapchain_handle;      // Dedicated swapchain texture handle for this window
    bool is_visible;
    bool is_pinned;              // Pinned in world space vs head-relative
    float opacity;               // Window opacity [0.0 .. 1.0]
};

struct PanelVertex {
    float pos[3];
    float uv[2];
    float normal[3];
};

/// Spatial Canvas Manager: Decomposes desktop into multiple independent 3D panels
class SpatialCanvasManager {
public:
    SpatialCanvasManager();
    ~SpatialCanvasManager();

    /// Creates a new floating 3D spatial window panel
    uint32_t CreatePanel(
        const std::string& title,
        float width_m,
        float height_m,
        const float pos[3],
        const float rot[4],
        float curvature = 0.0f
    );

    /// Destroys a spatial panel
    bool DestroyPanel(uint32_t panel_id);

    /// Updates 3D transform (position, rotation, scale)
    bool UpdatePanelTransform(
        uint32_t panel_id,
        const float pos[3],
        const float rot[4],
        float width_m,
        float height_m
    );

    /// Assigns a dedicated swapchain texture handle to this panel
    bool AssignSwapchain(uint32_t panel_id, void* swapchain_handle);

    /// Generates curved / flat mesh vertices for 3D rendering
    bool GeneratePanelMesh(
        uint32_t panel_id,
        uint32_t subdivisions_x,
        std::vector<PanelVertex>& out_vertices,
        std::vector<uint32_t>& out_indices
    );

    SpatialPanel* GetPanel(uint32_t panel_id);
    std::vector<SpatialPanel> GetAllPanels() const;
    size_t GetPanelCount() const;
    void ClearAllPanels();

private:
    uint32_t m_nextPanelId;
    mutable std::mutex m_mutex;
    std::vector<SpatialPanel> m_panels;
};

// =============================================================================
// Desk Passthrough Cutout (XR_FB_passthrough)
// =============================================================================

struct DeskCutoutBounds {
    float center[3];            // Center of desk surface (x, y, z in meters)
    float orientation[4];       // Desk orientation quaternion (x, y, z, w)
    float width_m;              // Desk width in meters (e.g. 1.0m)
    float depth_m;              // Desk depth/length in meters (e.g. 0.5m)
    float height_m;             // Height extrusion for 3D hands (e.g. 0.3m)
    float edge_feather_m;       // Soft boundary feathering width (e.g. 0.05m)
};

/// Desk & Keyboard Passthrough Cutout using Meta OpenXR XR_FB_passthrough
/// Opens a live color passthrough window onto the user's real physical desk & keyboard
/// within the immersive VR environment.
class DeskPassthroughCutout {
public:
    DeskPassthroughCutout();
    ~DeskPassthroughCutout();

    /// Initializes passthrough cutout with physical desk dimensions
    bool Initialize(const DeskCutoutBounds& bounds);

    /// Updates desk position and size (e.g. after user re-calibration)
    void UpdateBounds(const DeskCutoutBounds& bounds);

    /// Sets passthrough window opacity [0.0 = completely virtual, 1.0 = full passthrough]
    void SetOpacity(float opacity);

    /// Sets boundary highlight outline color (for visual cutout border)
    void SetEdgeBorderColor(float r, float g, float b, float a);

    /// Generates the geometric cutout mask polygon/mesh for compositor projection
    void GenerateCutoutMesh(
        std::vector<float>& out_vertices_xyz,
        std::vector<uint32_t>& out_indices
    );

    bool IsActive() const { return m_isActive; }
    void SetActive(bool active) { m_isActive = active; }
    float GetOpacity() const { return m_opacity; }
    const DeskCutoutBounds& GetBounds() const { return m_bounds; }
    const float* GetEdgeColor() const { return m_edgeColor; }

private:
    bool m_isActive;
    DeskCutoutBounds m_bounds;
    float m_opacity;
    float m_edgeColor[4]; // r, g, b, a
    mutable std::mutex m_mutex;
};

} // namespace vxr

#endif // VXR_SPATIAL_CANVAS_H
