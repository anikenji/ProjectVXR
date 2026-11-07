#include "spatial_canvas.h"
#include <cmath>
#include <algorithm>
#include <iostream>

namespace vxr {

// Quaternion rotation helper: rotates vector v by quaternion q [x, y, z, w]
static void RotateVectorByQuaternion(const float v[3], const float q[4], float out[3]) {
    // q * v * q^-1
    float qx = q[0], qy = q[1], qz = q[2], qw = q[3];

    // t = 2 * cross(q.xyz, v)
    float tx = 2.0f * (qy * v[2] - qz * v[1]);
    float ty = 2.0f * (qz * v[0] - qx * v[2]);
    float tz = 2.0f * (qx * v[1] - qy * v[0]);

    // v' = v + q.w * t + cross(q.xyz, t)
    out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
    out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
    out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
}

// =============================================================================
// SpatialCanvasManager
// =============================================================================

SpatialCanvasManager::SpatialCanvasManager()
    : m_nextPanelId(1)
{
}

SpatialCanvasManager::~SpatialCanvasManager() {
    ClearAllPanels();
}

uint32_t SpatialCanvasManager::CreatePanel(
    const std::string& title,
    float width_m,
    float height_m,
    const float pos[3],
    const float rot[4],
    float curvature
) {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t id = m_nextPanelId++;
    SpatialPanel panel;
    panel.panel_id = id;
    panel.title = title;
    panel.size[0] = width_m;
    panel.size[1] = height_m;
    panel.curvature_radius = curvature;
    panel.swapchain_handle = nullptr;
    panel.is_visible = true;
    panel.is_pinned = true;
    panel.opacity = 1.0f;

    for (int i = 0; i < 3; ++i) panel.position[i] = pos[i];
    for (int i = 0; i < 4; ++i) panel.orientation[i] = rot[i];

    m_panels.push_back(panel);

    std::cout << "[VXR-Spatial] Created 3D Panel #" << id << " '" << title
              << "' (" << width_m << "m x " << height_m << "m) at pos=("
              << pos[0] << ", " << pos[1] << ", " << pos[2] << ")" << std::endl;

    return id;
}

bool SpatialCanvasManager::DestroyPanel(uint32_t panel_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_panels.begin(), m_panels.end(), [panel_id](const SpatialPanel& p) {
        return p.panel_id == panel_id;
    });

    if (it != m_panels.end()) {
        std::cout << "[VXR-Spatial] Destroyed Panel #" << panel_id << " '" << it->title << "'" << std::endl;
        m_panels.erase(it);
        return true;
    }
    return false;
}

bool SpatialCanvasManager::UpdatePanelTransform(
    uint32_t panel_id,
    const float pos[3],
    const float rot[4],
    float width_m,
    float height_m
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_panels) {
        if (p.panel_id == panel_id) {
            if (pos) for (int i = 0; i < 3; ++i) p.position[i] = pos[i];
            if (rot) for (int i = 0; i < 4; ++i) p.orientation[i] = rot[i];
            if (width_m > 0.0f) p.size[0] = width_m;
            if (height_m > 0.0f) p.size[1] = height_m;
            return true;
        }
    }
    return false;
}

bool SpatialCanvasManager::AssignSwapchain(uint32_t panel_id, void* swapchain_handle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_panels) {
        if (p.panel_id == panel_id) {
            p.swapchain_handle = swapchain_handle;
            return true;
        }
    }
    return false;
}

bool SpatialCanvasManager::GeneratePanelMesh(
    uint32_t panel_id,
    uint32_t subdivisions_x,
    std::vector<PanelVertex>& out_vertices,
    std::vector<uint32_t>& out_indices
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const SpatialPanel* panel = nullptr;
    for (const auto& p : m_panels) {
        if (p.panel_id == panel_id) {
            panel = &p;
            break;
        }
    }

    if (!panel) return false;

    out_vertices.clear();
    out_indices.clear();

    float w = panel->size[0];
    float h = panel->size[1];
    float half_h = h * 0.5f;

    uint32_t subs = std::max(subdivisions_x, 1u);
    float step = 1.0f / static_cast<float>(subs);

    // Cylindrical curvature calculations
    float radius = panel->curvature_radius;
    bool is_curved = (radius > 0.01f);
    float arc_angle = is_curved ? (w / radius) : 0.0f;

    // Generate vertex grid (subs + 1 columns, 2 rows: top and bottom)
    for (uint32_t col = 0; col <= subs; ++col) {
        float u = col * step;

        float lx, lz;
        float nx, nz;

        if (is_curved) {
            float phi = (u - 0.5f) * arc_angle;
            lx = radius * std::sin(phi);
            lz = radius * (1.0f - std::cos(phi));
            nx = -std::sin(phi);
            nz = std::cos(phi);
        } else {
            lx = (u - 0.5f) * w;
            lz = 0.0f;
            nx = 0.0f;
            nz = 1.0f;
        }

        // Top vertex (v = 0.0)
        float local_top[3] = { lx, half_h, lz };
        float local_norm[3] = { nx, 0.0f, nz };
        float world_top[3], world_norm[3];
        RotateVectorByQuaternion(local_top, panel->orientation, world_top);
        RotateVectorByQuaternion(local_norm, panel->orientation, world_norm);
        world_top[0] += panel->position[0];
        world_top[1] += panel->position[1];
        world_top[2] += panel->position[2];

        PanelVertex v_top;
        v_top.pos[0] = world_top[0]; v_top.pos[1] = world_top[1]; v_top.pos[2] = world_top[2];
        v_top.uv[0] = u; v_top.uv[1] = 0.0f;
        v_top.normal[0] = world_norm[0]; v_top.normal[1] = world_norm[1]; v_top.normal[2] = world_norm[2];
        out_vertices.push_back(v_top);

        // Bottom vertex (v = 1.0)
        float local_bot[3] = { lx, -half_h, lz };
        float world_bot[3];
        RotateVectorByQuaternion(local_bot, panel->orientation, world_bot);
        world_bot[0] += panel->position[0];
        world_bot[1] += panel->position[1];
        world_bot[2] += panel->position[2];

        PanelVertex v_bot;
        v_bot.pos[0] = world_bot[0]; v_bot.pos[1] = world_bot[1]; v_bot.pos[2] = world_bot[2];
        v_bot.uv[0] = u; v_bot.uv[1] = 1.0f;
        v_bot.normal[0] = world_norm[0]; v_bot.normal[1] = world_norm[1]; v_bot.normal[2] = world_norm[2];
        out_vertices.push_back(v_bot);
    }

    // Generate indices
    for (uint32_t col = 0; col < subs; ++col) {
        uint32_t tl = col * 2;
        uint32_t bl = col * 2 + 1;
        uint32_t tr = (col + 1) * 2;
        uint32_t br = (col + 1) * 2 + 1;

        // Triangle 1: tl, bl, tr
        out_indices.push_back(tl);
        out_indices.push_back(bl);
        out_indices.push_back(tr);

        // Triangle 2: tr, bl, br
        out_indices.push_back(tr);
        out_indices.push_back(bl);
        out_indices.push_back(br);
    }

    return true;
}

SpatialPanel* SpatialCanvasManager::GetPanel(uint32_t panel_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_panels) {
        if (p.panel_id == panel_id) {
            return &p;
        }
    }
    return nullptr;
}

std::vector<SpatialPanel> SpatialCanvasManager::GetAllPanels() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_panels;
}

size_t SpatialCanvasManager::GetPanelCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_panels.size();
}

void SpatialCanvasManager::ClearAllPanels() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_panels.clear();
}

// =============================================================================
// DeskPassthroughCutout (XR_FB_passthrough)
// =============================================================================

DeskPassthroughCutout::DeskPassthroughCutout()
    : m_isActive(false)
    , m_opacity(1.0f)
{
    memset(&m_bounds, 0, sizeof(m_bounds));
    m_edgeColor[0] = 0.0f; // Cyan boundary border default
    m_edgeColor[1] = 0.8f;
    m_edgeColor[2] = 1.0f;
    m_edgeColor[3] = 0.8f;
}

DeskPassthroughCutout::~DeskPassthroughCutout() {
}

bool DeskPassthroughCutout::Initialize(const DeskCutoutBounds& bounds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bounds = bounds;
    m_isActive = true;

    std::cout << "[VXR-Passthrough] Initialized Desk Cutout: "
              << bounds.width_m << "m x " << bounds.depth_m << "m at pos=("
              << bounds.center[0] << ", " << bounds.center[1] << ", " << bounds.center[2] << ")"
              << ", Opacity=" << m_opacity << std::endl;

    return true;
}

void DeskPassthroughCutout::UpdateBounds(const DeskCutoutBounds& bounds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bounds = bounds;
}

void DeskPassthroughCutout::SetOpacity(float opacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_opacity = std::clamp(opacity, 0.0f, 1.0f);
}

void DeskPassthroughCutout::SetEdgeBorderColor(float r, float g, float b, float a) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edgeColor[0] = r;
    m_edgeColor[1] = g;
    m_edgeColor[2] = b;
    m_edgeColor[3] = a;
}

void DeskPassthroughCutout::GenerateCutoutMesh(
    std::vector<float>& out_vertices_xyz,
    std::vector<uint32_t>& out_indices
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    out_vertices_xyz.clear();
    out_indices.clear();

    float half_w = m_bounds.width_m * 0.5f;
    float half_d = m_bounds.depth_m * 0.5f;

    // 4 local desk surface corners
    const float localCorners[4][3] = {
        { -half_w, 0.0f, -half_d }, // 0: Front-Left
        {  half_w, 0.0f, -half_d }, // 1: Front-Right
        {  half_w, 0.0f,  half_d }, // 2: Back-Right
        { -half_w, 0.0f,  half_d }  // 3: Back-Left
    };

    for (int i = 0; i < 4; ++i) {
        float rotated[3];
        RotateVectorByQuaternion(localCorners[i], m_bounds.orientation, rotated);
        out_vertices_xyz.push_back(m_bounds.center[0] + rotated[0]);
        out_vertices_xyz.push_back(m_bounds.center[1] + rotated[1]);
        out_vertices_xyz.push_back(m_bounds.center[2] + rotated[2]);
    }

    // 2 triangles forming the desk surface cutout quad
    out_indices.push_back(0);
    out_indices.push_back(1);
    out_indices.push_back(2);

    out_indices.push_back(0);
    out_indices.push_back(2);
    out_indices.push_back(3);
}

} // namespace vxr
