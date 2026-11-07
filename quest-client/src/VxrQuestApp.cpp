#include "VxrQuestApp.h"
#include <iostream>
#include <cstring>

namespace vxr {

static constexpr uint32_t MENU_OVERLAY_W = 1280;
static constexpr uint32_t MENU_OVERLAY_H = 800;
static constexpr uint32_t PERF_HUD_W     = 480;
static constexpr uint32_t PERF_HUD_H     = 220;

VxrQuestApp::VxrQuestApp()
    : m_running(false)
    , m_totalFramesRendered(0)
    , m_prevMenuBtnPressed(false)
    , m_prevTriggerClicked(false)
    , m_surfacePointerX(0.5f)
    , m_surfacePointerY(0.5f)
    , m_hasSurfacePointer(false)
{
    memset(&m_config, 0, sizeof(m_config));
}

VxrQuestApp::~VxrQuestApp() {
    Shutdown();
}

bool VxrQuestApp::Initialize(const QuestAppConfig& config) {
    Shutdown();
    m_config = config;

    // 1. Initialize AMediaCodec hardware video decoder
    if (!m_decoder.Initialize(config.preferred_codec, config.stream_width, config.stream_height, config.target_fps)) {
        std::cerr << "[VXR-App] Failed to initialize AMediaCodec decoder!" << std::endl;
        return false;
    }

    // 2. Initialize Vulkan 1.3 renderer with AHardwareBuffer zero-copy import & overlay quads
    VulkanRendererConfig vkConfig = {};
    vkConfig.width = config.stream_width;
    vkConfig.height = config.stream_height;
    vkConfig.enable_validation_layers = false;
    vkConfig.enable_zero_copy_ahb = config.enable_zero_copy;
    vkConfig.msaa_samples = 4;

    if (!m_renderer.Initialize(vkConfig)) {
        std::cerr << "[VXR-App] Failed to initialize Vulkan renderer!" << std::endl;
        return false;
    }

    // 3. Initialize OpenXR Mobile client
    OpenXrClientConfig xrConfig = {};
    xrConfig.app_name = "Project VXR Quest Client";
    xrConfig.enable_space_warp_ssw = config.enable_space_warp;
    xrConfig.enable_foveated_rendering = config.enable_foveation;
    xrConfig.display_refresh_rate = config.target_fps;

    if (!m_openXr.Initialize(xrConfig)) {
        std::cerr << "[VXR-App] Failed to initialize OpenXR client!" << std::endl;
        return false;
    }

    // 4. Initialize Virtual Desktop 7-Tab In-VR Menu UI & RGBA Framebuffers
    m_vrMenu.Initialize();
    m_menuRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0);
    m_perfHudRgbaBuffer.assign(PERF_HUD_W * PERF_HUD_H, 0);

    m_vrMenu.RenderMenuToRgba(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);
    m_renderer.UpdateMenuOverlayTexture(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);

    VrPerfStats initialPerf = {};
    initialPerf.fps = config.target_fps;
    initialPerf.bitrate_mbps = m_vrMenu.GetSettings().vr_bitrate_mbps;
    initialPerf.ssw_active = config.enable_space_warp;
    m_vrMenu.RenderPerformanceOverlayToRgba(m_perfHudRgbaBuffer.data(), PERF_HUD_W, PERF_HUD_H, initialPerf);
    m_renderer.UpdatePerformanceHudTexture(m_perfHudRgbaBuffer.data(), PERF_HUD_W, PERF_HUD_H);

    // 5. Initialize UDP Network Client (:38810 video/control & :5353 mDNS discovery)
    m_network.Initialize(38810, 5353);

    m_running = true;
    m_totalFramesRendered = 0;
    m_prevMenuBtnPressed = false;
    m_prevTriggerClicked = false;
    m_surfacePointerX = 0.5f;
    m_surfacePointerY = 0.5f;
    m_hasSurfacePointer = false;

    std::cout << "[VXR-App] Meta Quest Client Pipeline initialized successfully: "
              << config.stream_width << "x" << config.stream_height << " @ " << config.target_fps << "Hz"
              << ", Zero-Copy: " << (config.enable_zero_copy ? "ENABLED" : "DISABLED")
              << ", SpaceWarp: " << (config.enable_space_warp ? "ENABLED" : "DISABLED")
              << ", In-VR Menu (7 Tabs): READY" << std::endl;

    return true;
}

bool VxrQuestApp::FeedVideoData(const uint8_t* data, size_t size, uint64_t pts_us, bool is_keyframe) {
    if (!m_running) return false;
    return m_decoder.QueueInputBuffer(data, size, pts_us, is_keyframe);
}

void VxrQuestApp::UpdateHardwareCursor(float norm_x, float norm_y, bool visible) {
    m_cursor.UpdatePosition(norm_x, norm_y, visible);
}

void VxrQuestApp::OnSurfacePointerEvent(float norm_x, float norm_y, bool is_down, bool is_click) {
    if (norm_x < 0.0f) norm_x = 0.0f;
    if (norm_x > 1.0f) norm_x = 1.0f;
    if (norm_y < 0.0f) norm_y = 0.0f;
    if (norm_y > 1.0f) norm_y = 1.0f;

    m_surfacePointerX = norm_x;
    m_surfacePointerY = norm_y;
    m_hasSurfacePointer = true;

    if (m_vrMenu.IsVisible()) {
        m_vrMenu.OnPointerMove(norm_x, norm_y);
        if (is_click) {
            m_vrMenu.OnPointerClick(norm_x, norm_y);
            // If user clicked Connect on a computer entry, trigger UDP discovery/handshake
            const auto& comps = m_vrMenu.GetComputers();
            for (const auto& c : comps) {
                if (c.connected && !c.ip_address.empty()) {
                    m_network.SendDiscoverAndConnect(c.ip_address.c_str(), 38810);
                }
            }
        }
    } else if (m_vrMenu.GetSettings().controllers_interact_desktop && m_network.IsConnected()) {
        uint8_t btnMask = is_down ? 0x01 : 0x00;
        m_network.SendDesktopMouseInput(norm_x, norm_y, btnMask, 0);
    }
}

const uint32_t* VxrQuestApp::GetCompositedRgbaFrame(uint32_t* out_width, uint32_t* out_height) {
    if (out_width) *out_width = MENU_OVERLAY_W;
    if (out_height) *out_height = MENU_OVERLAY_H;
    if (m_menuRgbaBuffer.size() < MENU_OVERLAY_W * MENU_OVERLAY_H) {
        m_menuRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF121418);
    }
    return m_menuRgbaBuffer.data();
}

bool VxrQuestApp::RunFrame() {
    if (!m_running || !m_openXr.IsSessionRunning()) {
        return false;
    }

    // 1. Poll mDNS Host Discovery & Incoming Fragmented Video Packets over UDP
    std::vector<DiscoveredHostInfo> discoveredHosts;
    if (m_network.PollMdnsDiscovery(&discoveredHosts) && !discoveredHosts.empty()) {
        std::vector<VrComputerEntry> comps;
        for (const auto& h : discoveredHosts) {
            VrComputerEntry entry;
            entry.hostname = h.computer_name;
            entry.ip_address = h.ip_address;
            entry.connected = (m_network.IsConnected() && m_network.GetConnectedHostIp() == h.ip_address);
            entry.available = true;
            entry.link_speed_mbps = 1200;
            entry.wifi_band_ghz = 5;
            comps.push_back(entry);
        }
        m_vrMenu.SetComputers(comps);
    }

    ReassembledVideoFrame netFrame = {};
    while (m_network.PollNetwork(&netFrame)) {
        if (netFrame.size > 0 && !netFrame.data.empty()) {
            m_decoder.QueueInputBuffer(netFrame.data.data(), netFrame.size, netFrame.pts_us, netFrame.is_keyframe);
            m_cursor.UpdatePosition(netFrame.cursor_norm_x, netFrame.cursor_norm_y, netFrame.cursor_visible);
        }
    }

    // 2. Wait for OpenXR frame timing
    uint64_t predictedDisplayTimeNs = 0;
    if (!m_openXr.WaitFrame(&predictedDisplayTimeNs)) {
        return false;
    }

    // 3. Begin OpenXR frame
    if (!m_openXr.BeginFrame()) {
        return false;
    }

    // 4. Locate 6DoF head eye poses
    XrEyeView eyeViews[2];
    m_openXr.LocateViews(predictedDisplayTimeNs, eyeViews);

    // 5. Poll 6DoF Touch Controller & Laser Pointer Input
    XrControllerInputState ctrl = {};
    m_openXr.PollControllerInput(&ctrl);

    if (ctrl.button_menu && !m_prevMenuBtnPressed) {
        m_vrMenu.ToggleVisible();
    }
    m_prevMenuBtnPressed = ctrl.button_menu;

    if (!m_hasSurfacePointer) {
        float pointerU = 0.5f;
        float pointerV = 0.5f;
        float screenDist = m_vrMenu.GetSettings().screen_distance_m;
        bool hitScreen = m_openXr.ComputeDesktopPointerUV(screenDist, &pointerU, &pointerV);

        if (hitScreen) {
            if (m_vrMenu.IsVisible()) {
                m_vrMenu.OnPointerMove(pointerU, pointerV);
                if (ctrl.trigger_clicked && !m_prevTriggerClicked) {
                    m_vrMenu.OnPointerClick(pointerU, pointerV);
                }
            } else if (m_vrMenu.GetSettings().controllers_interact_desktop && m_network.IsConnected()) {
                uint8_t btnMask = 0;
                if (ctrl.trigger_clicked || ctrl.button_a) btnMask |= 0x01; // Left click
                if (ctrl.button_b) btnMask |= 0x02;                         // Right click
                int16_t wheelDelta = 0;
                if (m_vrMenu.GetSettings().thumbstick_vertical_scroll && (ctrl.thumbstick_y > 0.15f || ctrl.thumbstick_y < -0.15f)) {
                    wheelDelta = static_cast<int16_t>(ctrl.thumbstick_y * 120.0f);
                }
                m_network.SendDesktopMouseInput(pointerU, pointerV, btnMask, wheelDelta);
            }
        }
    }
    m_prevTriggerClicked = ctrl.trigger_clicked;

    // 6. Sync In-VR Menu Changes & Actions to PC Host & Local Renderer
    auto& menuCfg = m_vrMenu.GetSettings();
    if (menuCfg.launch_steamvr_requested) {
        m_network.SendLaunchSteamVr();
        menuCfg.launch_steamvr_requested = false;
    }
    if (m_vrMenu.IsSettingsDirty()) {
        m_renderer.SetEnvironmentPreset(
            menuCfg.selected_environment,
            menuCfg.screen_distance_m,
            menuCfg.screen_curvature
        );
        m_network.SendClientStreamingSettings(
            menuCfg.desktop_bitrate_mbps,
            menuCfg.vr_bitrate_mbps,
            menuCfg.vr_graphics_quality,
            menuCfg.vr_frame_rate,
            menuCfg.sharpening_pct,
            static_cast<uint8_t>(menuCfg.ssw_mode),
            menuCfg.snapdragon_sgsr
        );
        m_vrMenu.ClearSettingsDirty();
    }

    // 7. Update Menu & Performance Overlay Textures
    if (m_vrMenu.IsVisible()) {
        m_vrMenu.RenderMenuToRgba(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);
        m_renderer.UpdateMenuOverlayTexture(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);
    }
    if (menuCfg.show_performance_overlay) {
        VrPerfStats stats = {};
        stats.fps = menuCfg.vr_frame_rate;
        stats.bitrate_mbps = menuCfg.vr_bitrate_mbps;
        stats.ssw_active = (menuCfg.ssw_mode != 0);
        m_vrMenu.RenderPerformanceOverlayToRgba(m_perfHudRgbaBuffer.data(), PERF_HUD_W, PERF_HUD_H, stats);
        m_renderer.UpdatePerformanceHudTexture(m_perfHudRgbaBuffer.data(), PERF_HUD_W, PERF_HUD_H);
    }

    // 8. Dequeue decoded frame from AMediaCodec & Render Vulkan Compositor Layers
    DecodedOutputFrame outputFrame = {};
    DecoderStatus decStatus = m_decoder.DequeueOutputBuffer(&outputFrame, 0);

    m_renderer.BeginFrame();

    if (decStatus == DecoderStatus::Success) {
        VulkanTexture streamTexture = {};
        if (m_renderer.ImportHardwareBuffer(outputFrame.hardware_buffer, outputFrame.width, outputFrame.height, &streamTexture)) {
            // Render stereo stream quads
            m_renderer.RenderStreamQuad(streamTexture, 0);
            m_renderer.RenderStreamQuad(streamTexture, 1);

            // Render 0ms Hardware Cursor overlay
            m_renderer.RenderCursorQuad(
                m_cursor.GetNormalizedX(),
                m_cursor.GetNormalizedY(),
                0.035f,
                0.035f,
                m_cursor.IsVisible()
            );

            m_renderer.ReleaseTexture(&streamTexture);
        }
        m_decoder.ReleaseOutputBuffer(outputFrame.buffer_index, false);
    }

    // Always render In-VR Menu Overlay Quad & Performance HUD Quad even before stream starts
    m_renderer.RenderMenuOverlayQuad(m_vrMenu.IsVisible(), menuCfg.show_performance_overlay);

    m_renderer.EndFrame();

    // 9. End OpenXR frame and submit layers to compositor
    m_openXr.EndFrame(predictedDisplayTimeNs);
    m_totalFramesRendered++;

    return true;
}

void VxrQuestApp::Shutdown() {
    if (m_running) {
        m_network.Cleanup();
        m_openXr.Cleanup();
        m_renderer.Cleanup();
        m_decoder.Cleanup();
        m_running = false;
    }
}

} // namespace vxr
