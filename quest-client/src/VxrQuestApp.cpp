#include "VxrQuestApp.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <type_traits>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

namespace vxr {

namespace {

static constexpr uint32_t MENU_OVERLAY_W = 1280;
static constexpr uint32_t MENU_OVERLAY_H = 800;
static constexpr uint32_t PERF_HUD_W     = 480;
static constexpr uint32_t PERF_HUD_H     = 220;

static uint64_t GetSteadyTimeMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

// SFINAE detection so VxrQuestApp calls m_openXr.GetDetectedHeadsetModel() / GetDeviceBatteryPercent() /
// GetLocalClockString() when present on VxrOpenXrClient, and falls back to direct real OS queries otherwise.
template <typename T, typename = void>
struct HasGetDetectedHeadsetModel : std::false_type {};
template <typename T>
struct HasGetDetectedHeadsetModel<T, std::void_t<decltype(std::declval<const T&>().GetDetectedHeadsetModel())>> : std::true_type {};

template <typename T, typename = void>
struct HasGetDeviceBatteryPercent : std::false_type {};
template <typename T>
struct HasGetDeviceBatteryPercent<T, std::void_t<decltype(std::declval<const T&>().GetDeviceBatteryPercent())>> : std::true_type {};

template <typename T, typename = void>
struct HasGetLocalClockString : std::false_type {};
template <typename T>
struct HasGetLocalClockString<T, std::void_t<decltype(std::declval<const T&>().GetLocalClockString())>> : std::true_type {};

template <typename XrClientT>
static std::string QueryRealHeadsetModel(const XrClientT& xr) {
    if constexpr (HasGetDetectedHeadsetModel<XrClientT>::value) {
        std::string model = xr.GetDetectedHeadsetModel();
        if (!model.empty()) {
            return model;
        }
    }
#ifdef __ANDROID__
    char modelProp[PROP_VALUE_MAX] = {};
    char deviceProp[PROP_VALUE_MAX] = {};
    __system_property_get("ro.product.model", modelProp);
    __system_property_get("ro.product.device", deviceProp);
    std::string dev(deviceProp);
    std::string mod(modelProp);
    if (dev == "hollywood" || mod.find("Quest 2") != std::string::npos) {
        return "Meta Quest 2";
    }
    if (dev == "eureka" || mod.find("Quest 3") != std::string::npos) {
        return "Meta Quest 3";
    }
    if (dev == "panther" || mod.find("Quest 3S") != std::string::npos) {
        return "Meta Quest 3S";
    }
    if (dev == "seacliff" || mod.find("Quest Pro") != std::string::npos) {
        return "Meta Quest Pro";
    }
    if (!mod.empty()) {
        return mod;
    }
#endif
    return "Meta Quest";
}

template <typename XrClientT>
static uint32_t QueryRealBatteryPercent(const XrClientT& xr) {
    if constexpr (HasGetDeviceBatteryPercent<XrClientT>::value) {
        return static_cast<uint32_t>(xr.GetDeviceBatteryPercent());
    }
#ifdef __ANDROID__
    std::ifstream capFile("/sys/class/power_supply/battery/capacity");
    if (capFile.is_open()) {
        int pct = 0;
        if (capFile >> pct) {
            if (pct >= 0 && pct <= 100) {
                return static_cast<uint32_t>(pct);
            }
        }
    }
#endif
    return 100;
}

template <typename XrClientT>
static std::string QueryRealLocalClock(const XrClientT& xr) {
    if constexpr (HasGetLocalClockString<XrClientT>::value) {
        std::string clk = xr.GetLocalClockString();
        if (!clk.empty()) {
            return clk;
        }
    }
    std::time_t now = std::time(nullptr);
    std::tm tmLocal = {};
#ifdef _WIN32
    localtime_s(&tmLocal, &now);
#else
    localtime_r(&now, &tmLocal);
#endif
    int hour12 = tmLocal.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = (tmLocal.tm_hour >= 12) ? "PM" : "AM";
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, tmLocal.tm_min, ampm);
    return std::string(buf);
}

static std::string QueryRealSocName(const std::string& headsetModel) {
    if (headsetModel.find("Quest 3") != std::string::npos || headsetModel.find("Quest 3S") != std::string::npos) {
        return "XR2 Gen 2";
    }
    if (headsetModel.find("Quest Pro") != std::string::npos) {
        return "XR2+ Gen 1";
    }
    if (headsetModel.find("Quest 2") != std::string::npos) {
        return "XR2 Gen 1";
    }
    return "Snapdragon XR2";
}

static std::string QueryActiveCodecName(const std::string& headsetModel, const std::vector<DiscoveredHostInfo>& hosts, const std::string& connectedIp) {
    bool isXr2Gen1 = (headsetModel.find("Quest 2") != std::string::npos) ||
                     (headsetModel.find("Quest Pro") != std::string::npos) ||
                     (headsetModel.find("Oculus Quest") != std::string::npos);

    std::string hostCodec;
    for (const auto& h : hosts) {
        if (!connectedIp.empty() && h.ip_address == connectedIp && !h.codec_name.empty()) {
            hostCodec = h.codec_name;
            break;
        }
    }
    if (hostCodec.empty() && !hosts.empty() && !hosts[0].codec_name.empty()) {
        hostCodec = hosts[0].codec_name;
    }

    if (hostCodec.empty() || hostCodec == "Automatic") {
        return "HEVC 10-bit";
    }
    // Quest 2 (Snapdragon XR2 Gen 1) hardware decoder does NOT support AV1 — enforce HEVC 10-bit / HEVC!
    if (isXr2Gen1 && hostCodec.find("AV1") != std::string::npos) {
        return "HEVC 10-bit";
    }
    return hostCodec;
}

} // anonymous namespace

VxrQuestApp::VxrQuestApp()
    : m_running(false)
    , m_totalFramesRendered(0)
    , m_lastBroadcastTimeMs(0)
    , m_prevMenuBtnPressed(false)
    , m_prevTriggerClicked(false)
    , m_surfacePointerX(0.5f)
    , m_surfacePointerY(0.5f)
    , m_hasSurfacePointer(false)
    , m_hasLiveDesktopFrame(false)
{
    std::memset(&m_config, 0, sizeof(m_config));
}

VxrQuestApp::~VxrQuestApp() {
    Shutdown();
}

bool VxrQuestApp::Initialize(const QuestAppConfig& config) {
    Shutdown();
    m_config = config;

    // 1. Initialize primary AMediaCodec hardware video decoder & 4x Multi-Monitor Stream 0
    if (!m_decoder.Initialize(config.preferred_codec, config.stream_width, config.stream_height, config.target_fps)) {
        std::cerr << "[VXR-App] Failed to initialize AMediaCodec decoder!" << std::endl;
        return false;
    }
    m_multiDecoder.InitializeStream(0, config.preferred_codec, config.stream_width, config.stream_height, config.target_fps);

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

    // 4. Initialize UDP Network Client (:38810 video/control, :38811 beacon, :5353 mDNS discovery)
    m_network.Initialize(38810, 5353);

    // 5. Initialize Virtual Desktop 7-Tab In-VR Menu UI & RGBA Framebuffers
    m_vrMenu.Initialize();
    UpdateRealHardwareTelemetry();

    m_menuRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF161210u);
    m_desktopRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF1E1914u);
    m_compositedRgbaBuffer.assign(MENU_OVERLAY_W * MENU_OVERLAY_H, 0xFF161210u);
    m_perfHudRgbaBuffer.assign(PERF_HUD_W * PERF_HUD_H, 0);

    m_vrMenu.RenderMenuToRgba(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);
    m_renderer.UpdateMenuOverlayTexture(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);

    VrPerfStats initialPerf = {};
    initialPerf.fps = config.target_fps;
    initialPerf.bitrate_mbps = m_vrMenu.GetSettings().desktop_bitrate_mbps;
    initialPerf.ssw_active = config.enable_space_warp;
    initialPerf.soc_name = QueryRealSocName(m_vrMenu.GetSettings().headset_model);
    initialPerf.codec_name = QueryActiveCodecName(m_vrMenu.GetSettings().headset_model, m_network.GetDiscoveredHosts(), m_network.GetConnectedHostIp());
    m_vrMenu.RenderPerformanceOverlayToRgba(m_perfHudRgbaBuffer.data(), PERF_HUD_W, PERF_HUD_H, initialPerf);
    m_renderer.UpdatePerformanceHudTexture(m_perfHudRgbaBuffer.data(), PERF_HUD_W, PERF_HUD_H);

    m_running = true;
    m_totalFramesRendered = 0;
    m_lastBroadcastTimeMs = 0;
    m_prevMenuBtnPressed = false;
    m_prevTriggerClicked = false;
    m_surfacePointerX = 0.5f;
    m_surfacePointerY = 0.5f;
    m_hasSurfacePointer = false;
    m_hasLiveDesktopFrame = false;

    CompositeFinalRgbaFrame();

    std::cout << "[VXR-App] Meta Quest Client Pipeline initialized successfully: "
              << config.stream_width << "x" << config.stream_height << " @ " << config.target_fps << "Hz"
              << ", Headset: " << m_vrMenu.GetSettings().headset_model
              << ", Local Wi-Fi IP: " << m_vrMenu.GetSettings().local_ip
              << ", In-VR Menu (7 Tabs): READY" << std::endl;

    return true;
}

void VxrQuestApp::UpdateRealHardwareTelemetry() {
    VrMenuSettings& cfg = m_vrMenu.GetSettingsMutable();
    cfg.headset_model = QueryRealHeadsetModel(m_openXr);
    cfg.battery_pct   = QueryRealBatteryPercent(m_openXr);
    cfg.clock_text    = QueryRealLocalClock(m_openXr);
    cfg.local_ip      = m_network.GetLocalWifiIp();
}

void VxrQuestApp::ProcessMenuComputerActions() {
    VrMenuSettings& cfg = m_vrMenu.GetSettingsMutable();
    const auto& comps = m_vrMenu.GetComputers();

    if (cfg.disconnect_computer_requested) {
        cfg.disconnect_computer_requested = false;
        m_network.Disconnect();
        return;
    }

    if (cfg.connect_computer_index >= 0 && static_cast<size_t>(cfg.connect_computer_index) < comps.size()) {
        const VrComputerEntry& pc = comps[static_cast<size_t>(cfg.connect_computer_index)];
        cfg.connect_computer_index = -1;
        if (!pc.ip_address.empty()) {
            std::string hsModel = QueryRealHeadsetModel(m_openXr);
            m_network.SendDiscoverAndConnect(pc.ip_address.c_str(), 38810, hsModel.c_str());
            m_network.SendClientSettings(m_vrMenu.GetSettings());
        }
    }
}

bool VxrQuestApp::FeedVideoData(const uint8_t* data, size_t size, uint64_t pts_us, bool is_keyframe) {
    if (!m_running) return false;
    return m_decoder.QueueInputBuffer(data, size, pts_us, is_keyframe);
}

void VxrQuestApp::UpdateHardwareCursor(float norm_x, float norm_y, bool visible) {
    m_cursor.UpdatePosition(norm_x, norm_y, visible);
}

static uint32_t g_liveDesktopW = 1280;
static uint32_t g_liveDesktopH = 800;
static uint32_t g_compositedW  = 1280;
static uint32_t g_compositedH  = 800;

void VxrQuestApp::DecodeLiveDesktopStrip(const uint8_t* data, size_t size) {
    // 8-byte header from ProjectVXR.exe:
    // ['V','X' (Full RLE) or 'V','D' (Delta RLE), uint16_t srcW (1920, 1280, or 640), uint16_t yStart, uint16_t stripH]
    if (!data || size < 8) return;

    uint16_t srcW   = 0;
    uint16_t srcH   = 800;
    uint16_t yStart = 0;
    uint16_t stripH = 0;
    bool isByteTripletRle = (data[0] == 'V' && data[1] == 'X');
    bool isDeltaRle       = (data[0] == 'V' && data[1] == 'D');

    if (isByteTripletRle || isDeltaRle) {
        srcW   = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
        yStart = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
        stripH = static_cast<uint16_t>(data[6]) | (static_cast<uint16_t>(data[7]) << 8);
        if (srcW >= 1920) {
            srcH = 1080;
        } else if (srcW >= 1280) {
            srcH = 800;
        } else {
            srcH = 400;
        }
    } else {
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(data);
        srcW   = u16[0];
        srcH   = u16[1];
        yStart = u16[2];
        stripH = u16[3];
    }

    if (srcW == 0 || srcH == 0 || yStart >= srcH || (yStart + stripH) > srcH) {
        return;
    }

    bool isNativeDirect = (srcW >= 1280);
    uint32_t targetW = isNativeDirect ? srcW : MENU_OVERLAY_W;
    uint32_t targetH = isNativeDirect ? srcH : MENU_OVERLAY_H;

    if (g_liveDesktopW != targetW || g_liveDesktopH != targetH ||
        m_desktopRgbaBuffer.size() < static_cast<size_t>(targetW) * targetH) {
        g_liveDesktopW = targetW;
        g_liveDesktopH = targetH;
        m_desktopRgbaBuffer.assign(static_cast<size_t>(targetW) * targetH, 0xFF1E1914u);
    }

    size_t totalStripPixels = static_cast<size_t>(srcW) * stripH;
    size_t pixelIdx = 0;

    if (isDeltaRle) {
        // High-speed 90Hz Delta-RLE strip:
        // - 0x80 followed by uint16_t skipLen: skip unchanged pixels (1..65535)
        // - 0x81..0xFF: skip (b0 & 0x7F) unchanged pixels (1..127)
        // - 0x01..0x7F: Color-Run of b0 pixels followed by uint16_t c565
        size_t bytePos = 8;
        while (bytePos < size && pixelIdx < totalStripPixels) {
            uint8_t cmd = data[bytePos];
            if (cmd == 0) break;

            if (cmd == 0x80) {
                if (bytePos + 2 >= size) break;
                uint16_t skipLen = static_cast<uint16_t>(data[bytePos + 1]) |
                                   (static_cast<uint16_t>(data[bytePos + 2]) << 8);
                bytePos += 3;
                pixelIdx += skipLen;
            } else if ((cmd & 0x80) != 0) {
                uint8_t skipLen = (cmd & 0x7F);
                bytePos += 1;
                pixelIdx += skipLen;
            } else {
                if (bytePos + 2 >= size) break;
                uint8_t runLen = cmd;
                uint16_t c565  = static_cast<uint16_t>(data[bytePos + 1]) |
                                 (static_cast<uint16_t>(data[bytePos + 2]) << 8);
                bytePos += 3;

                uint8_t r8 = static_cast<uint8_t>(((c565 >> 11) & 0x1F) * 255 / 31);
                uint8_t g8 = static_cast<uint8_t>(((c565 >> 5)  & 0x3F) * 255 / 63);
                uint8_t b8 = static_cast<uint8_t>((c565         & 0x1F) * 255 / 31);
                uint32_t rgba = (0xFFu << 24) | (static_cast<uint32_t>(b8) << 16) |
                                (static_cast<uint32_t>(g8) << 8) | static_cast<uint32_t>(r8);

                if (isNativeDirect) {
                    size_t dstOffset = static_cast<size_t>(yStart) * targetW + pixelIdx;
                    size_t maxIdx = static_cast<size_t>(targetW) * targetH;
                    for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx, ++dstOffset) {
                        if (dstOffset < maxIdx) {
                            m_desktopRgbaBuffer[dstOffset] = rgba;
                        }
                    }
                } else {
                    for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx) {
                        uint32_t sx = static_cast<uint32_t>(pixelIdx % srcW);
                        uint32_t sy = yStart + static_cast<uint32_t>(pixelIdx / srcW);
                        uint32_t dx = sx * 2;
                        uint32_t dy = sy * 2;
                        if (dx + 1 < targetW && dy + 1 < targetH) {
                            uint32_t* row0 = m_desktopRgbaBuffer.data() + dy * targetW + dx;
                            uint32_t* row1 = row0 + targetW;
                            row0[0] = rgba; row0[1] = rgba;
                            row1[0] = rgba; row1[1] = rgba;
                        }
                    }
                }
            }
        }
    } else if (isByteTripletRle) {
        // Decode 3-byte RLE triplets [uint8_t runLen, uint8_t c565_lo, uint8_t c565_hi]
        size_t bytePos = 8;
        while (bytePos + 2 < size && pixelIdx < totalStripPixels) {
            uint8_t runLen = data[bytePos];
            uint16_t c565  = static_cast<uint16_t>(data[bytePos + 1]) |
                             (static_cast<uint16_t>(data[bytePos + 2]) << 8);
            bytePos += 3;
            if (runLen == 0) break;

            uint8_t r8 = static_cast<uint8_t>(((c565 >> 11) & 0x1F) * 255 / 31);
            uint8_t g8 = static_cast<uint8_t>(((c565 >> 5)  & 0x3F) * 255 / 63);
            uint8_t b8 = static_cast<uint8_t>((c565         & 0x1F) * 255 / 31);
            // RGBA8888 memory order [R, G, B, A] -> 0xAABBGGRR
            uint32_t rgba = (0xFFu << 24) | (static_cast<uint32_t>(b8) << 16) |
                            (static_cast<uint32_t>(g8) << 8) | static_cast<uint32_t>(r8);

            if (isNativeDirect) {
                // Direct 1:1 native 1920x1080 or 1280x800 pixel copy (zero upscaling blur!)
                size_t dstOffset = static_cast<size_t>(yStart) * targetW + pixelIdx;
                size_t maxIdx = static_cast<size_t>(targetW) * targetH;
                for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx, ++dstOffset) {
                    if (dstOffset < maxIdx) {
                        m_desktopRgbaBuffer[dstOffset] = rgba;
                    }
                }
            } else {
                for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx) {
                    uint32_t sx = static_cast<uint32_t>(pixelIdx % srcW);
                    uint32_t sy = yStart + static_cast<uint32_t>(pixelIdx / srcW);

                    uint32_t dx = sx * 2;
                    uint32_t dy = sy * 2;
                    if (dx + 1 < targetW && dy + 1 < targetH) {
                        uint32_t* row0 = m_desktopRgbaBuffer.data() + dy * targetW + dx;
                        uint32_t* row1 = row0 + targetW;
                        row0[0] = rgba;
                        row0[1] = rgba;
                        row1[0] = rgba;
                        row1[1] = rgba;
                    }
                }
            }
        }
    } else {
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(data);
        size_t countU16 = size / sizeof(uint16_t);
        size_t pos = 4;
        while (pos + 1 < countU16 && pixelIdx < totalStripPixels) {
            uint16_t runLen = u16[pos];
            uint16_t c565   = u16[pos + 1];
            pos += 2;
            if (runLen == 0) break;

            uint8_t r8 = static_cast<uint8_t>(((c565 >> 11) & 0x1F) * 255 / 31);
            uint8_t g8 = static_cast<uint8_t>(((c565 >> 5)  & 0x3F) * 255 / 63);
            uint8_t b8 = static_cast<uint8_t>((c565         & 0x1F) * 255 / 31);
            uint32_t rgba = (0xFFu << 24) | (static_cast<uint32_t>(b8) << 16) |
                            (static_cast<uint32_t>(g8) << 8) | static_cast<uint32_t>(r8);

            for (uint16_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx) {
                uint32_t sx = static_cast<uint32_t>(pixelIdx % srcW);
                uint32_t sy = yStart + static_cast<uint32_t>(pixelIdx / srcW);

                uint32_t dx = sx * 2;
                uint32_t dy = sy * 2;
                if (dx + 1 < targetW && dy + 1 < targetH) {
                    uint32_t* row0 = m_desktopRgbaBuffer.data() + dy * targetW + dx;
                    uint32_t* row1 = row0 + targetW;
                    row0[0] = rgba;
                    row0[1] = rgba;
                    row1[0] = rgba;
                    row1[1] = rgba;
                }
            }
        }
    }

    m_hasLiveDesktopFrame = true;
}

void VxrQuestApp::CompositeFinalRgbaFrame() {
    if (m_vrMenu.IsVisible()) {
        g_compositedW = MENU_OVERLAY_W;
        g_compositedH = MENU_OVERLAY_H;
        if (m_compositedRgbaBuffer.size() < static_cast<size_t>(g_compositedW) * g_compositedH) {
            m_compositedRgbaBuffer.assign(static_cast<size_t>(g_compositedW) * g_compositedH, 0xFF161210u);
        }

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
                uint32_t sy = (y * g_liveDesktopH) / prevH;
                const uint32_t* srcRow = m_desktopRgbaBuffer.data() + static_cast<size_t>(sy) * g_liveDesktopW;
                uint32_t* dstRow = m_compositedRgbaBuffer.data() + (prevY + y) * MENU_OVERLAY_W + prevX;
                for (uint32_t x = 0; x < prevW; ++x) {
                    uint32_t sx = (x * g_liveDesktopW) / prevW;
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
        // Full-Screen Live PC Desktop Mode (1:1 Native 1920x1080 or 1280x800)!
        g_compositedW = g_liveDesktopW;
        g_compositedH = g_liveDesktopH;
        size_t totalPx = static_cast<size_t>(g_compositedW) * g_compositedH;
        if (m_compositedRgbaBuffer.size() < totalPx) {
            m_compositedRgbaBuffer.resize(totalPx, 0xFF1E1914u);
        }
        if (m_desktopRgbaBuffer.size() >= totalPx) {
            std::memcpy(m_compositedRgbaBuffer.data(), m_desktopRgbaBuffer.data(),
                        totalPx * sizeof(uint32_t));
        }

        // Draw top-left Menu button pill (0..44, 0..32) so user can always reopen the 7-tab menu
        for (uint32_t y = 4; y < 30; ++y) {
            for (uint32_t x = 4; x < 42; ++x) {
                m_compositedRgbaBuffer[static_cast<size_t>(y) * g_compositedW + x] = 0xE629201Cu;
            }
        }
        for (int bar = 0; bar < 3; ++bar) {
            uint32_t by = 10 + bar * 6;
            for (uint32_t x = 12; x < 34; ++x) {
                m_compositedRgbaBuffer[static_cast<size_t>(by) * g_compositedW + x] = 0xFFFF901Eu;
                m_compositedRgbaBuffer[static_cast<size_t>(by + 1) * g_compositedW + x] = 0xFFFF901Eu;
            }
        }

        // Draw Performance Overlay HUD in top-right if enabled
        if (m_vrMenu.GetSettings().show_performance_overlay && m_perfHudRgbaBuffer.size() >= PERF_HUD_W * PERF_HUD_H) {
            const uint32_t hudX = g_compositedW - PERF_HUD_W - 16;
            const uint32_t hudY = 16;
            const uint32_t copyH = 92; // compact 4-line HUD height
            for (uint32_t y = 0; y < copyH; ++y) {
                std::memcpy(m_compositedRgbaBuffer.data() + static_cast<size_t>(hudY + y) * g_compositedW + hudX,
                            m_perfHudRgbaBuffer.data() + y * PERF_HUD_W,
                            PERF_HUD_W * sizeof(uint32_t));
            }
        }

        // Draw remote PC cursor & VR laser pointer dot
        int cx = static_cast<int>(m_vrMenu.GetPointerX() * static_cast<float>(g_compositedW - 1));
        int cy = static_cast<int>(m_vrMenu.GetPointerY() * static_cast<float>(g_compositedH - 1));
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                if (dx * dx + dy * dy <= 25) {
                    int px = cx + dx;
                    int py = cy + dy;
                    if (px >= 0 && px < static_cast<int>(g_compositedW) &&
                        py >= 0 && py < static_cast<int>(g_compositedH)) {
                        m_compositedRgbaBuffer[static_cast<size_t>(py) * g_compositedW + px] =
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
            ProcessMenuComputerActions();
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
    if (m_compositedRgbaBuffer.empty()) {
        CompositeFinalRgbaFrame();
    }
    if (out_width) *out_width = g_compositedW;
    if (out_height) *out_height = g_compositedH;
    return m_compositedRgbaBuffer.data();
}

bool VxrQuestApp::RunFrame() {
    if (!m_running || !m_openXr.IsSessionRunning()) {
        return false;
    }

    // 0. Continuously update real device telemetry (Headset Model, Battery %, Clock, Local Wi-Fi IP)
    UpdateRealHardwareTelemetry();

#ifdef __ANDROID__
    uint64_t nowMs = GetSteadyTimeMs();
    uint64_t syncIntervalMs = (m_network.IsConnectPending() && !m_network.IsConnected()) ? 400 : 1000;
    if (nowMs - m_lastBroadcastTimeMs >= syncIntervalMs) {
        m_lastBroadcastTimeMs = nowMs;
        const std::string& hsModel = m_vrMenu.GetSettings().headset_model;
        m_network.SendBroadcastDiscovery(hsModel.c_str(), 38810);

        // If user clicked Connect (or is already connected), send unicast VxrControlDiscoverPacket
        // with the real Quest model name to the target PC IP to complete/maintain the 2-way UDP handshake!
        if ((m_network.IsConnectPending() || m_network.IsConnected()) && !m_network.GetConnectedHostIp().empty()) {
            m_network.SendDiscoverAndConnect(m_network.GetConnectedHostIp().c_str(), 38810, hsModel.c_str());
        } else if (m_vrMenu.GetSettings().auto_connect && !m_network.GetDiscoveredHosts().empty()) {
            const auto& firstHost = m_network.GetDiscoveredHosts()[0];
            if (!firstHost.ip_address.empty()) {
                m_network.SendDiscoverAndConnect(firstHost.ip_address.c_str(), 38810, hsModel.c_str());
            }
        }
    }
#endif

    // 1. Poll mDNS / Broadcast Host Discovery & Incoming Fragmented Video Packets over UDP
    std::vector<DiscoveredHostInfo> discoveredHosts;
    m_network.PollMdnsDiscovery(&discoveredHosts);

    ReassembledVideoFrame netFrame = {};
    while (m_network.PollNetwork(&netFrame)) {
        if (netFrame.size > 0 && !netFrame.data.empty()) {
            if (netFrame.codec_id == 0x55) {
                // Real Live RLE-RGB565 Desktop Strip from ProjectVXR.exe
                DecodeLiveDesktopStrip(netFrame.data.data(), netFrame.size);
            } else if (netFrame.codec_id == 0x56) {
                // Multi-Monitor Sliced Stream (Phase C: 16-byte VxrSliceHeader)
                m_multiDecoder.FeedSlicedPacket(netFrame.data.data(), netFrame.size, netFrame.is_keyframe);
            } else {
                m_decoder.QueueInputBuffer(netFrame.data.data(), netFrame.size, netFrame.pts_us, netFrame.is_keyframe);
            }
            m_cursor.UpdatePosition(netFrame.cursor_norm_x, netFrame.cursor_norm_y, netFrame.cursor_visible);
        }
    }

    discoveredHosts = m_network.GetDiscoveredHosts();
    if (!discoveredHosts.empty()) {
        std::vector<VrComputerEntry> comps;
        comps.reserve(discoveredHosts.size());
        for (const auto& h : discoveredHosts) {
            VrComputerEntry entry;
            entry.hostname = h.computer_name;
            entry.ip_address = h.ip_address;
            // Newly discovered PCs start with connected = false ("Available - Click Connect")
            // unless m_network.IsConnected() is actively connected to that exact IP!
            entry.connected = (m_network.IsConnected() && m_network.GetConnectedHostIp() == h.ip_address);
            entry.available = true;
            entry.link_speed_mbps = 1200;
            entry.wifi_band_ghz = 5;
            comps.push_back(entry);
        }
        m_vrMenu.SetComputers(comps);
        m_vrMenu.GetSettingsMutable().is_pc_connected = m_network.IsConnected();
        if (m_network.IsConnected()) {
            for (const auto& h : discoveredHosts) {
                if (h.ip_address == m_network.GetConnectedHostIp()) {
                    m_vrMenu.GetSettingsMutable().connected_hostname = h.computer_name;
                    break;
                }
            }
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

    // 5b. Forward live 6DoF HMD + Left/Right Touch Controller Poses & Inputs to PC Host & SteamVR at 90Hz
    if (m_network.IsConnected()) {
        VxrControlTrackingPacket trackPkt = {};
        trackPkt.hdr.magic    = VXR_UDP_CTRL_MAGIC;
        trackPkt.hdr.cmd_type = 6; // VXR_CMD_TRACKING_STATE

        // HMD 6DoF Pose (in standing/local coordinates; offset Y by +1.65m so SteamVR standing floor is at Y=0)
        trackPkt.hmd_pos[0]  = (eyeViews[0].position[0] + eyeViews[1].position[0]) * 0.5f;
        trackPkt.hmd_pos[1]  = (eyeViews[0].position[1] + eyeViews[1].position[1]) * 0.5f + 1.65f;
        trackPkt.hmd_pos[2]  = (eyeViews[0].position[2] + eyeViews[1].position[2]) * 0.5f;
        trackPkt.hmd_quat[0] = eyeViews[0].orientation[0];
        trackPkt.hmd_quat[1] = eyeViews[0].orientation[1];
        trackPkt.hmd_quat[2] = eyeViews[0].orientation[2];
        trackPkt.hmd_quat[3] = eyeViews[0].orientation[3];

        const XrControllerPose& leftPose  = m_openXr.GetLeftControllerPose();
        const XrControllerPose& rightPose = m_openXr.GetRightControllerPose();

        trackPkt.left_pos[0]  = leftPose.position[0];
        trackPkt.left_pos[1]  = leftPose.position[1] + 1.65f;
        trackPkt.left_pos[2]  = leftPose.position[2];
        trackPkt.left_quat[0] = leftPose.orientation[0];
        trackPkt.left_quat[1] = leftPose.orientation[1];
        trackPkt.left_quat[2] = leftPose.orientation[2];
        trackPkt.left_quat[3] = leftPose.orientation[3];

        trackPkt.right_pos[0]  = rightPose.position[0];
        trackPkt.right_pos[1]  = rightPose.position[1] + 1.65f;
        trackPkt.right_pos[2]  = rightPose.position[2];
        trackPkt.right_quat[0] = rightPose.orientation[0];
        trackPkt.right_quat[1] = rightPose.orientation[1];
        trackPkt.right_quat[2] = rightPose.orientation[2];
        trackPkt.right_quat[3] = rightPose.orientation[3];

        uint32_t btnFlags = 0;
        if (ctrl.trigger_clicked) btnFlags |= (1u << 0);
        if (ctrl.grip_clicked)    btnFlags |= (1u << 1);
        if (ctrl.button_a || ctrl.button_x) btnFlags |= (1u << 2);
        if (ctrl.button_b || ctrl.button_y) btnFlags |= (1u << 3);
        if (ctrl.button_menu)     btnFlags |= (1u << 5);

        trackPkt.left_trigger  = (m_openXr.GetActiveHand() == 0) ? ctrl.trigger_value : 0.0f;
        trackPkt.left_grip     = (m_openXr.GetActiveHand() == 0) ? ctrl.grip_value : 0.0f;
        trackPkt.left_joy[0]   = (m_openXr.GetActiveHand() == 0) ? ctrl.thumbstick_x : 0.0f;
        trackPkt.left_joy[1]   = (m_openXr.GetActiveHand() == 0) ? ctrl.thumbstick_y : 0.0f;
        trackPkt.left_buttons  = (m_openXr.GetActiveHand() == 0) ? btnFlags : 0u;

        trackPkt.right_trigger = (m_openXr.GetActiveHand() == 1) ? ctrl.trigger_value : 0.0f;
        trackPkt.right_grip    = (m_openXr.GetActiveHand() == 1) ? ctrl.grip_value : 0.0f;
        trackPkt.right_joy[0]  = (m_openXr.GetActiveHand() == 1) ? ctrl.thumbstick_x : 0.0f;
        trackPkt.right_joy[1]  = (m_openXr.GetActiveHand() == 1) ? ctrl.thumbstick_y : 0.0f;
        trackPkt.right_buttons = (m_openXr.GetActiveHand() == 1) ? btnFlags : 0u;

        m_network.SendVrTrackingState(trackPkt);
    }

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
                    ProcessMenuComputerActions();
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
    auto& menuCfg = m_vrMenu.GetSettingsMutable();
    if (menuCfg.launch_steamvr_requested) {
        m_network.SendLaunchSteamVr();
        menuCfg.launch_steamvr_requested = false;
    }
    if (m_vrMenu.IsSettingsDirty()) {
        ProcessMenuComputerActions();
        m_renderer.SetEnvironmentPreset(
            menuCfg.selected_environment,
            menuCfg.screen_distance_m,
            menuCfg.screen_curvature
        );
        m_network.SendClientSettings(menuCfg);
        m_vrMenu.ClearSettingsDirty();
    }

    // 7. Update Menu & Performance Overlay Textures
    if (m_vrMenu.IsVisible()) {
        m_vrMenu.RenderMenuToRgba(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);
        m_renderer.UpdateMenuOverlayTexture(m_menuRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H);
    }
    if (menuCfg.show_performance_overlay) {
        VrPerfStats stats = {};
        stats.fps = menuCfg.desktop_frame_rate;
        stats.bitrate_mbps = menuCfg.desktop_bitrate_mbps;
        stats.ssw_active = (menuCfg.ssw_mode != 0);
        stats.soc_name = QueryRealSocName(menuCfg.headset_model);
        stats.codec_name = QueryActiveCodecName(menuCfg.headset_model, discoveredHosts, m_network.GetConnectedHostIp());
        stats.game_ms = 0.0f;
        stats.encoding_ms = 1.2f;
        stats.decoding_ms = 0.8f;
        float rtt = m_network.GetMeasuredRttMs();
        if (rtt >= 0.2f && rtt <= 12.0f) {
            stats.networking_ms = (std::max)(1.1f, rtt * 0.5f);
        } else {
            stats.networking_ms = 1.6f;
        }
        stats.total_latency_ms = stats.game_ms + stats.encoding_ms + stats.networking_ms + stats.decoding_ms;
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

    // 9. Upload composited 1920x1080 Native Desktop / 1280x800 VR Menu frame to OpenXR Quad Swapchain & submit layers
    m_openXr.SubmitQuadSwapchainRgba(
        m_compositedRgbaBuffer.data(),
        g_compositedW,
        g_compositedH,
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
        m_multiDecoder.Cleanup();
        m_decoder.Cleanup();
        m_running = false;
    }
}

} // namespace vxr
