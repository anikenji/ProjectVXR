#ifdef NDEBUG
#undef NDEBUG
#endif

#include "spatial_canvas.h"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "   Project VXR: Spatial Canvas & Cutout Test     " << std::endl;
    std::cout << "=================================================" << std::endl;

    // Test 1: Decomposing Desktop into Multiple 3D Spatial Panels
    std::cout << "[Test 1] Testing Spatial Panel creation (Code Editor, Browser, Discord)..." << std::endl;
    {
        vxr::SpatialCanvasManager canvas;
        assert(canvas.GetPanelCount() == 0);

        // Panel 1: Center Code Editor (VS Code) - curved cylindrical panel
        float centerPos[3] = { 0.0f, 1.6f, -1.8f };
        float identityRot[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        uint32_t codeEditorId = canvas.CreatePanel("Code Editor", 1.4f, 0.9f, centerPos, identityRot, 2.5f);
        assert(codeEditorId == 1);

        // Panel 2: Left Web Browser - angled 30 degrees right toward user
        float leftPos[3] = { -1.4f, 1.6f, -1.6f };
        // Rotation ~25 deg around Y: sin(12.5 deg) = 0.2164, cos(12.5 deg) = 0.9763
        float leftRot[4] = { 0.0f, 0.2164f, 0.0f, 0.9763f };
        uint32_t browserId = canvas.CreatePanel("Web Browser", 1.2f, 0.8f, leftPos, leftRot, 0.0f);
        assert(browserId == 2);

        // Panel 3: Right Discord / Communications - angled 25 degrees left toward user
        float rightPos[3] = { 1.4f, 1.6f, -1.6f };
        float rightRot[4] = { 0.0f, -0.2164f, 0.0f, 0.9763f };
        uint32_t discordId = canvas.CreatePanel("Discord", 0.9f, 0.8f, rightPos, rightRot, 0.0f);
        assert(discordId == 3);

        assert(canvas.GetPanelCount() == 3);
        std::cout << "  -> Successfully spawned 3 concurrent spatial panels in 3D VR space." << std::endl;

        // Test 2: Individual Swapchain Handle Assignment
        std::cout << "[Test 2] Testing individual swapchain handles per 3D window..." << std::endl;
        void* scCode = reinterpret_cast<void*>(0x11110000ULL);
        void* scBrowser = reinterpret_cast<void*>(0x22220000ULL);
        void* scDiscord = reinterpret_cast<void*>(0x33330000ULL);

        assert(canvas.AssignSwapchain(codeEditorId, scCode));
        assert(canvas.AssignSwapchain(browserId, scBrowser));
        assert(canvas.AssignSwapchain(discordId, scDiscord));

        auto pCode = canvas.GetPanel(codeEditorId);
        auto pBrowser = canvas.GetPanel(browserId);
        auto pDiscord = canvas.GetPanel(discordId);

        assert(pCode->swapchain_handle == scCode);
        assert(pBrowser->swapchain_handle == scBrowser);
        assert(pDiscord->swapchain_handle == scDiscord);
        std::cout << "  -> Verified independent swapchain video streams per spatial window." << std::endl;

        // Test 3: Curved Cylindrical Mesh vs Flat Mesh Generation
        std::cout << "[Test 3] Testing curved cylindrical vs flat panel mesh generation..." << std::endl;
        std::vector<vxr::PanelVertex> flatVerts;
        std::vector<uint32_t> flatIndices;
        assert(canvas.GeneratePanelMesh(browserId, 1, flatVerts, flatIndices));
        assert(flatVerts.size() == 4);    // 4 corner vertices
        assert(flatIndices.size() == 6);  // 2 triangles (quad)

        std::vector<vxr::PanelVertex> curvedVerts;
        std::vector<uint32_t> curvedIndices;
        assert(canvas.GeneratePanelMesh(codeEditorId, 16, curvedVerts, curvedIndices));
        assert(curvedVerts.size() == 34);  // (16 + 1) * 2 = 34 vertices
        assert(curvedIndices.size() == 16 * 6); // 16 quads * 6 = 96 indices

        // Center vertex of curved panel should have different Z than outer edges
        float edgeZ = curvedVerts[0].pos[2];
        float midZ = curvedVerts[16].pos[2]; // column 8 (center)
        assert(std::abs(edgeZ - midZ) > 0.01f);
        std::cout << "  -> Cylindrical mesh generated with smooth curvature delta="
                  << std::abs(edgeZ - midZ) << "m" << std::endl;

        // Test 4: Dynamic Repositioning & Panel Deletion
        std::cout << "[Test 4] Testing panel repositioning and destruction..." << std::endl;
        float newPos[3] = { 0.0f, 1.5f, -1.2f }; // Pull closer to user
        assert(canvas.UpdatePanelTransform(codeEditorId, newPos, nullptr, 1.5f, 1.0f));
        assert(pCode->position[2] == -1.2f);
        assert(pCode->size[0] == 1.5f);

        assert(canvas.DestroyPanel(discordId));
        assert(canvas.GetPanelCount() == 2);
        assert(canvas.GetPanel(discordId) == nullptr);
        std::cout << "  -> Repositioning and dynamic window destruction verified." << std::endl;
    }

    // Test 5: Desk & Keyboard Passthrough Cutout (XR_FB_passthrough)
    std::cout << "[Test 5] Testing Desk & Keyboard Passthrough Cutout (XR_FB_passthrough)..." << std::endl;
    {
        vxr::DeskPassthroughCutout desk;
        assert(!desk.IsActive());

        vxr::DeskCutoutBounds bounds;
        bounds.center[0] = 0.0f;
        bounds.center[1] = 0.75f; // Standard desk height 75 cm
        bounds.center[2] = -0.45f;// 45 cm in front of user
        bounds.orientation[0] = 0.0f;
        bounds.orientation[1] = 0.0f;
        bounds.orientation[2] = 0.0f;
        bounds.orientation[3] = 1.0f; // Horizontal level
        bounds.width_m = 1.0f;  // 1.0 meter wide desk
        bounds.depth_m = 0.5f;  // 0.5 meter deep (keyboard + mouse zone)
        bounds.height_m = 0.25f; // 25 cm hand height clearance
        bounds.edge_feather_m = 0.05f;

        assert(desk.Initialize(bounds));
        assert(desk.IsActive());

        desk.SetOpacity(0.9f);
        assert(desk.GetOpacity() == 0.9f);

        desk.SetEdgeBorderColor(0.0f, 1.0f, 0.5f, 0.8f);
        assert(desk.GetEdgeColor()[1] == 1.0f);

        // Generate Cutout Geometry for OpenXR passthrough layer
        std::vector<float> cutoutVerts;
        std::vector<uint32_t> cutoutIndices;
        desk.GenerateCutoutMesh(cutoutVerts, cutoutIndices);

        assert(cutoutVerts.size() == 12); // 4 vertices * 3 coords (x, y, z)
        assert(cutoutIndices.size() == 6); // 2 triangles

        // Verify desk surface coordinates
        float frontLeftX = cutoutVerts[0];
        float frontLeftY = cutoutVerts[1];
        float frontLeftZ = cutoutVerts[2];

        // Front-left corner: center X - W/2 = 0 - 0.5 = -0.5m
        assert(std::abs(frontLeftX - (-0.5f)) < 1e-4);
        // Height: 0.75m
        assert(std::abs(frontLeftY - 0.75f) < 1e-4);
        // Depth: center Z - D/2 = -0.45 - 0.25 = -0.70m
        assert(std::abs(frontLeftZ - (-0.70f)) < 1e-4);

        std::cout << "  -> Desk Passthrough Cutout generated: 1.0m x 0.5m surface at Y=0.75m" << std::endl;
        std::cout << "  -> Physical keyboard/mouse passthrough window verified!" << std::endl;
    }

    std::cout << "=================================================" << std::endl;
    std::cout << "   ALL SPATIAL CANVAS & CUTOUT TESTS PASSED!     " << std::endl;
    std::cout << "=================================================" << std::endl;
    return 0;
}
