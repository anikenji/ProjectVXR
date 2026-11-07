#include "VxrQuestApp.h"
#include <iostream>
#include <cstring>
#include <algorithm>

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
    , m_hasLiveDesktopFrame(false)
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
    m_menuRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF161210u);
    m_desktopRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF1E1914u);
    m_compositedRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF161210u);
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
    m_hasLiveDesktopFrame = false;

    CompositeFinalRgbaFrame();

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

void VxrQuestApp::DecodeLiveDesktopStrip(const uint8_t* data, size_t size) {
    // 8-byte header: [uint16_t srcW, uint16_t srcH, uint16_t yStart, uint16_t stripH]
    if (!data || size < 8) return;
    const uint16_t* u16 = reinterpret_cast<const uint16_t*>(data);
    size_t countU16 = size / sizeof(uint16_t);

    uint16_t srcW   = u16[0];
    uint16_t srcH   = u16[1];
    uint16_t yStart = u16[2];
    uint16_t stripH = u16[3];

    if (srcW == 0 || srcH == 0 || yStart >= srcH || (yStart + stripH) > srcH) {
        return;
    }

    if (m_desktopRgbaBuffer.size() < MENU_OVERLAY_W * MENU_OVERLAY_H) {
        m_desktopRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF1E1914u);
    }

    size_t totalStripPixels = static_cast<size_t>(srcW) * stripH;
    size_t pixelIdx = 0;
    size_t pos = 4;

    // Decode RLE-RGB565 pairs (runLen, c565) and 2x-upscale (640x400 -> 1280x800) into m_desktopRgbaBuffer
    while (pos + 1 < countU16 && pixelIdx < totalStripPixels) {
        uint16_t runLen = u16[pos];
        uint16_t c565   = u16[pos + 1];
        pos += 2;
        if (runLen == 0) break;

        uint8_t r8 = static_cast<uint8_t>(((c565 >> 11) & 0x1F) * 255 / 31);
        uint8_t g8 = static_cast<uint8_t>(((c565 >> 5)  & 0x3F) * 255 / 63);
        uint8_t b8 = static_cast<uint8_t>((c565         & 0x1F) * 255 / 31);
        // RGBA8888 memory order [R, G, B, A] -> 0xAABBGGRR
        uint32_t rgba = (0xFFu << 24) | (static_cast<uint32_t>(b8) << 16) |
                        (static_cast<uint32_t>(g8) << 8) | static_cast<uint32_t>(r8);

        for (uint16_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx) {
            uint32_t sx = static_cast<uint32_t>(pixelIdx % srcW);
            uint32_t sy = yStart + static_cast<uint32_t>(pixelIdx / srcW);

            uint32_t dx = sx * 2;
            uint32_t dy = sy * 2;
            if (dx + 1 < MENU_OVERLAY_W && dy + 1 < MENU_OVERLAY_H) {
                uint32_t* row0 = m_desktopRgbaBuffer.data() + dy * MENU_OVERLAY_W + dx;
                uint32_t* row1 = row0 + MENU_OVERLAY_W;
                row0[0] = rgba;
                row0[1] = rgba;
                row1[0] = rgba;
                row1[1] = rgba;
            }
        }
    }

    m_hasLiveDesktopFrame = true;
}

void VxrQuestApp::CompositeFinalRgbaFrame() {
    if (m_compositedRgbaBuffer.size() < MENU_OVERLAY_W * MENU_OVERLAY_H) {
        m_compositedRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF161210u);
    }

    if (m_vrMenu.IsVisible()) {
        // Copy 7-tab menu buffer
        std::memcpy(m_compositedRgbaBuffer.data(), m_menuRgbaBuffer.data(),
                    MENU_OVERLAY_W * MENU_OVERLAY_H * sizeof(uint32_t));

        // If on COMPUTERS tab and we have a live desktop stream from the PC, draw a live 560x350 preview window!
        if (m_vrMenu.GetActiveTab() == VrMenuTab::COMPUTERS && m_hasLiveDesktopFrame) {
            const uint32_t prevW = 560;
            const uint32_t prevH = 350;
            const uint32_t prevX = 260;
            const uint32_t prevY = 360;
            for (uint32_t y = 0; y < prevH; ++y) {
                uint32_t sy = (y * MENU_OVERLAY_H) / prevH;
                const uint32_t* srcRow = m_desktopRgbaBuffer.data() + sy * MENU_OVERLAY_W;
                uint32_t* dstRow = m_compositedRgbaBuffer.data() + (prevY + y) * MENU_OVERLAY_W + prevX;
                for (uint32_t x = 0; x < prevW; ++x) {
                    uint32_t sx = (x * MENU_OVERLAY_W) / prevW;
                    dstRow[x] = srcRow[sx];
                }
            }
            // Draw DodgerBlue border around live preview
            const uint32_t borderCol = 0xFFFF901Eu; // Rgba(0x1E, 0x90, 0xFF)
            for (uint32_t x = 0; x < prevW; ++x) {
                m_compositedRgbaBuffer[(prevY) * MENU_OVERLAY_W + prevX + x] = borderCol;
                m_compositedRgbaBuffer[(prevY + prevH - 1) * MENU_OVERLAY_W + prevX + x] = borderCol;
            }
            for (uint32_t y = 0; y < prevH; ++y) {
                m_compositedRgbaBuffer[(prevY + y) * MENU_OVERLAY_W + prevX] = borderCol;
                m_compositedRgbaBuffer[(prevY + y) * MENU_OVERLAY_W + prevX + prevW - 1] = borderCol;
            }
        }
    } else {
        // Full-Screen Live PC Desktop Mode!
        std::memcpy(m_compositedRgbaBuffer.data(), m_desktopRgbaBuffer.data(),
                    MENU_OVERLAY_W * MENU_OVERLAY_H * sizeof(uint32_t));

        // Draw top-left Menu button pill (0..44, 0..32) so user can always reopen the 7-tab menu
        for (uint32_t y = 4; y < 30; ++y) {
            for (uint32_t x = 4; x < 42; ++x) {
                m_compositedRgbaBuffer[y * MENU_OVERLAY_W + x] = 0xE629201Cu;
            }
        }
        for (int bar = 0; bar < 3; ++bar) {
            uint32_t by = 10 + bar * 6;
            for (uint32_t x = 12; x < 34; ++x) {
                m_compositedRgbaBuffer[by * MENU_OVERLAY_W + x] = 0xFFFF901Eu;
                m_compositedRgbaBuffer[(by + 1) * MENU_OVERLAY_W + x] = 0xFFFF901Eu;
            }
        }

        // Draw Performance Overlay HUD in top-right if enabled
        if (m_vrMenu.GetSettings().show_performance_overlay && m_perfHudRgbaBuffer.size() >= PERF_HUD_W * PERF_HUD_H) {
            const uint32_t hudX = MENU_OVERLAY_W - PERF_HUD_W - 16;
            const uint32_t hudY = 16;
            const uint32_t copyH = 92; // compact 4-line HUD height
            for (uint32_t y = 0; y < copyH; ++y) {
                std::memcpy(m_compositedRgbaBuffer.data() + (hudY + y) * MENU_OVERLAY_W + hudX,
                            m_perfHudRgbaBuffer.data() + y * PERF_HUD_W,
                            PERF_HUD_W * sizeof(uint32_t));
            }
        }

        // Draw remote PC cursor & VR laser pointer dot
        int cx = static_cast<int>(m_vrMenu.GetPointerX() * static_cast<float>(MENU_OVERLAY_W - 1));
        int cy = static_cast<int>(m_vrMenu.GetPointerY() * static_cast<float>(MENU_OVERLAY_H - 1));
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                if (dx * dx + dy * dy <= 25) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < static_cast<int>(MENU_OVERLAY_W) &&
                        py >= 0 && py < static_cast<int>(MENU_OVERLAY_H)) {
                        m_compositedRgbaBuffer[py * MENU_OVERLAY_W + px] =
                            (dx * dx + dy * dy >= 16) ? 0xFFFFFFFFu : 0xFFFF901Eu;
                    }
                }
            }
        }
    }
}

void VxrQuestApp::OnSurfacePointerEvent(float norm_x, float norm_y, bool is_down, bool is_click) {
    if (norm_x < 0.0f) norm_x = 0.0f;
    if (norm_x > 1.0f) norm_x = 1.0f;
    if (norm_y < 0.0f) norm_y = 0.0f;
    if (norm_y > 1.0f) norm_y = 1.0f;

    m_surfacePointerX = norm_x;
    m_surfacePointerY = norm_y;
    m_hasSurfacePointer = true;
    m_vrMenu.OnPointerMove(norm_x, norm_y);

    if (m_vrMenu.IsVisible()) {
        if (is_click) {
            m_vrMenu.OnPointerClick(norm_x, norm_y);
            const auto& comps = m_vrMenu.GetComputers();
            for (const auto& c : comps) {
                if (c.connected && !c.ip_address.empty()) {
                    m_network.SendDiscoverAndConnect(c.ip_address.c_str(), 38810);
                }
            }
        }
    } else {
        // Top-left menu button pill (norm_x < 0.04, norm_y < 0.05) reopens the 7-tab menu
        if (is_click && norm_x < 0.04f && norm_y < 0.05f) {
            m_vrMenu.SetVisible(true);
            return;
        }
        if (m_vrMenu.GetSettings().controllers_interact_desktop && m_network.IsConnected()) {
            uint8_t btnMask = is_down ? 0x01 : 0x00;
            m_network.SendDesktopMouseInput(norm_x, norm_y, btnMask, 0);
        }
    }
}

const uint32_t* VxrQuestApp::GetCompositedRgbaFrame(uint32_t* out_width, uint32_t* out_height) {
    if (out_width) *out_width = MENU_OVERLAY_W;
    if (out_height) *out_height = MENU_OVERLAY_H;
    if (m_compositedRgbaBuffer.size() < MENU_OVERLAY_W * MENU_OVERLAY_H) {
        CompositeFinalRgbaFrame();
    }
    return m_compositedRgbaBuffer.data();
}

bool VxrQuestApp::RunFrame() {
    if (!m_running || !m_openXr.IsSessionRunning()) {
        return false;
    }

    // Periodically send LAN discovery broadcast every ~120 frames (~1.3s) until a host is found
    if (!m_network.IsConnected() && (m_totalFramesRendered % 120) == 0) {
        m_network.SendDiscoverAndConnect("255.255.255.255", 38810);
    }

    // 1. Poll mDNS Host Discovery & Incoming Fragmented Video Packets over UDP
    std::vector<DiscoveredHostInfo> discoveredHosts;
    bool newMdns = m_network.PollMdnsDiscovery(&discoveredHosts);
    if (discoveredHosts.empty()) {
        discoveredHosts = m_network.GetDiscoveredHosts();
    }
    if (!discoveredHosts.empty()) {
        // Auto-connect to the first discovered PC if auto_connect is enabled and not yet connected
        if (!m_network.IsConnected() && m_vrMenu.GetSettings().auto_connect) {
            m_network.SendDiscoverAndConnect(discoveredHosts[0].ip_address.c_str(), discoveredHosts[0].port);
        }

        if (newMdns || m_vrMenu.GetComputers().size() != discoveredHosts.size()) {
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
            m_vrMenu.GetSettings().local_ip = discoveredHosts[0].ip_address;
            m_vrMenu.GetSettings().connected_hostname = discoveredHosts[0].computer_name;
        }
    }

    ReassembledVideoFrame netFrame = {};
    while (m_network.PollNetwork(&netFrame)) {
        if (netFrame.size > 0 && !netFrame.data.empty()) {
            if (netFrame.codec_id == 0x55) {
                // Real Live RLE-RGB565 Desktop Strip from ProjectVXR.exe
                DecodeLiveDesktopStrip(netFrame.data.data(), netFrame.size);
            } else {
                m_decoder.QueueInputBuffer(netFrame.data.data(), netFrame.size, netFrame.pts_us, netFrame.is_keyframe);
            }
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

    if (!m_hasSurfacePointer || m_openXr.IsRealRuntimeActive()) {
        float pointerU = 0.5f;
        float pointerV = 0.5f;
        float screenDist = m_vrMenu.GetSettings().screen_distance_m;
        bool hitScreen = m_openXr.ComputeDesktopPointerUV(screenDist, &pointerU, &pointerV);

        if (hitScreen) {
            m_vrMenu.OnPointerMove(pointerU, pointerV);
            if (m_vrMenu.IsVisible()) {
                if (ctrl.trigger_clicked && !m_prevTriggerClicked) {
                    m_vrMenu.OnPointerClick(pointerU, pointerV);
                    const auto& comps = m_vrMenu.GetComputers();
                    for (const auto& c : comps) {
                        if (c.connected && !c.ip_address.empty()) {
                            m_network.SendDiscoverAndConnect(c.ip_address.c_str(), 38810);
                        }
                    }
                }
            } else {
                if (ctrl.trigger_clicked && !m_prevTriggerClicked && pointerU < 0.04f && pointerV < 0.05f) {
                    m_vrMenu.SetVisible(true);
                } else if (m_vrMenu.GetSettings().controllers_interact_desktop && m_network.IsConnected()) {
                    uint8_t btnMask = 0;
                    if (ctrl.trigger_clicked || ctrl.button_a) btnMask |= 0x01; // Left click
                    if (ctrl.button_b) btnMask |= 0x02;                         // Right click
                    int16_t wheelDelta = 0;
                    if (m_vrMenu.GetSettings().thumbstick_vertical_scroll &&
                        (ctrl.thumbstick_y > 0.15f || ctrl.thumbstick_y < -0.15f)) {
                        wheelDelta = static_cast<int16_t>(ctrl.thumbstick_y * 120.0f);
                    }
                    m_network.SendDesktopMouseInput(pointerU, pointerV, btnMask, wheelDelta);
                }
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

    CompositeFinalRgbaFrame();

    // 8. Dequeue decoded frame from AMediaCodec & Render Vulkan Compositor Layers
    DecodedOutputFrame outputFrame = {};
    DecoderStatus decStatus = m_decoder.DequeueOutputBuffer(&outputFrame, 0);

    m_renderer.BeginFrame();

    if (decStatus == DecoderStatus::Success) {
        VulkanTexture streamTexture = {};
        if (m_renderer.ImportHardwareBuffer(outputFrame.hardware_buffer, outputFrame.width, outputFrame.height, &streamTexture)) {
            m_renderer.RenderStreamQuad(streamTexture, 0);
            m_renderer.RenderStreamQuad(streamTexture, 1);

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

    m_renderer.RenderMenuOverlayQuad(m_vrMenu.IsVisible(), menuCfg.show_performance_overlay);
    m_renderer.EndFrame();

    // 9. Upload composited 1280x800 frame to OpenXR Quad Swapchain & submit layers to VR compositor
    m_openXr.SubmitQuadSwapchainRgba(
        m_compositedRgbaBuffer.data(),
        MENU_OVERLAY_W,
        MENU_OVERLAY_H,
        menuCfg.screen_distance_m,
        menuCfg.screen_size_scale
    );
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
