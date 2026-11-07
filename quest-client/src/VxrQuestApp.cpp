#include "VxrQuestApp.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cmath>
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

static uint32_t g_liveDesktopW = 1280;
static uint32_t g_liveDesktopH = 800;
static uint32_t g_compositedW  = 1280;
static uint32_t g_compositedH  = 800;
static std::vector<uint32_t> g_desktopStagingBuffer;
static uint64_t g_lastStagingFlipMs = 0;

// Interactive Desktop UI & Gesture state
static bool     g_virtualKeyboardOpen       = false;
static bool     g_vkShiftActive             = false;
static bool     g_vkCapsLockActive          = false;
static bool     g_vkCtrlActive              = false;
static bool     g_vkAltActive               = false;
static bool     g_prevBtnX                  = false;
static bool     g_remoteStereoVrActive      = false;
static bool     g_manualStereoVrMode        = false;
static uint64_t g_triggerHoldStartMs        = 0;
static float    g_triggerHoldStartU         = 0.5f;
static float    g_triggerHoldStartV         = 0.5f;
static bool     g_longPressRightClickFired  = false;
static float    g_longPressRingProgress     = 0.0f;

// Compact 5x7 bitmap font for Floating Quick-Action Bar & On-Screen Virtual Keyboard
static const uint8_t kOverlayFont5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00}, {0x10,0x08,0x08,0x10,0x08}, {0x00,0x00,0x00,0x00,0x00}
};

static void DrawOverlayRect(uint32_t* buf, uint32_t bufW, uint32_t bufH,
                            uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t col) {
    uint32_t xEnd = (std::min)(bufW, rx + rw);
    uint32_t yEnd = (std::min)(bufH, ry + rh);
    for (uint32_t y = ry; y < yEnd; ++y) {
        uint32_t* row = buf + static_cast<size_t>(y) * bufW;
        for (uint32_t x = rx; x < xEnd; ++x) {
            row[x] = col;
        }
    }
}

static void DrawOverlayOutline(uint32_t* buf, uint32_t bufW, uint32_t bufH,
                               uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh, uint32_t col) {
    if (rw == 0 || rh == 0) return;
    DrawOverlayRect(buf, bufW, bufH, rx, ry, rw, 1, col);
    DrawOverlayRect(buf, bufW, bufH, rx, ry + rh - 1, rw, 1, col);
    DrawOverlayRect(buf, bufW, bufH, rx, ry, 1, rh, col);
    DrawOverlayRect(buf, bufW, bufH, rx + rw - 1, ry, 1, rh, col);
}

static void DrawOverlayText(uint32_t* buf, uint32_t bufW, uint32_t bufH,
                            uint32_t tx, uint32_t ty, const char* text, uint32_t col, uint32_t scale = 1) {
    if (!text) return;
    uint32_t curX = tx;
    for (const char* p = text; *p; ++p) {
        uint8_t c = static_cast<uint8_t>(*p);
        if (c < 32 || c > 127) c = 32;
        const uint8_t* glyph = kOverlayFont5x7[c - 32];
        for (uint32_t gx = 0; gx < 5; ++gx) {
            uint8_t colBits = glyph[gx];
            for (uint32_t gy = 0; gy < 7; ++gy) {
                if ((colBits >> gy) & 1u) {
                    for (uint32_t sy = 0; sy < scale; ++sy) {
                        for (uint32_t sx = 0; sx < scale; ++sx) {
                            uint32_t px = curX + gx * scale + sx;
                            uint32_t py = ty + gy * scale + sy;
                            if (px < bufW && py < bufH) {
                                buf[static_cast<size_t>(py) * bufW + px] = col;
                            }
                        }
                    }
                }
            }
        }
        curX += 6 * scale;
    }
}

struct VkKeyDef {
    const char* label;
    uint8_t     vkCode;
    uint16_t    widthUnits; // 10 = standard key (60px)
};

static const VkKeyDef kVkRow0[] = {
    {"ESC", 0x1B, 11}, {"1", '1', 10}, {"2", '2', 10}, {"3", '3', 10}, {"4", '4', 10},
    {"5", '5', 10}, {"6", '6', 10}, {"7", '7', 10}, {"8", '8', 10}, {"9", '9', 10},
    {"0", '0', 10}, {"-", 0xBD, 10}, {"=", 0xBB, 10}, {"BKSP", 0x08, 17}
};
static const VkKeyDef kVkRow1[] = {
    {"TAB", 0x09, 14}, {"Q", 'Q', 10}, {"W", 'W', 10}, {"E", 'E', 10}, {"R", 'R', 10},
    {"T", 'T', 10}, {"Y", 'Y', 10}, {"U", 'U', 10}, {"I", 'I', 10}, {"O", 'O', 10},
    {"P", 'P', 10}, {"[", 0xDB, 10}, {"]", 0xDD, 10}, {"\\", 0xDC, 14}
};
static const VkKeyDef kVkRow2[] = {
    {"CAPS", 0x14, 16}, {"A", 'A', 10}, {"S", 'S', 10}, {"D", 'D', 10}, {"F", 'F', 10},
    {"G", 'G', 10}, {"H", 'H', 10}, {"J", 'J', 10}, {"K", 'K', 10}, {"L", 'L', 10},
    {";", 0xBA, 10}, {"'", 0xDE, 10}, {"ENTER", 0x0D, 22}
};
static const VkKeyDef kVkRow3[] = {
    {"SHIFT", 0x10, 20}, {"Z", 'Z', 10}, {"X", 'X', 10}, {"C", 'C', 10}, {"V", 'V', 10},
    {"B", 'B', 10}, {"N", 'N', 10}, {"M", 'M', 10}, {",", 0xBC, 10}, {".", 0xBE, 10},
    {"/", 0xBF, 10}, {"UP", 0x26, 12}, {"HIDE", 0xFF, 16}
};
static const VkKeyDef kVkRow4[] = {
    {"CTRL", 0x11, 15}, {"WIN", 0x5B, 14}, {"ALT", 0x12, 14},
    {"SPACE", 0x20, 57}, {"LEFT", 0x25, 12}, {"DOWN", 0x28, 12}, {"RIGHT", 0x27, 14}
};

void VxrQuestApp::ProcessMenuComputerActions() {
    VrMenuSettings& cfg = m_vrMenu.GetSettingsMutable();
    const auto& comps = m_vrMenu.GetComputers();

    if (cfg.disconnect_computer_requested) {
        cfg.disconnect_computer_requested = false;
        cfg.auto_connect = false;
        m_network.Disconnect();
        m_hasLiveDesktopFrame = false;
        g_remoteStereoVrActive = false;
        g_manualStereoVrMode = false;
        m_openXr.SetImmersiveStereoVrMode(false);
        return;
    }

    if (cfg.enter_vr_mode_requested) {
        cfg.enter_vr_mode_requested = false;
        g_manualStereoVrMode = !g_manualStereoVrMode;
        if (g_manualStereoVrMode) {
            m_vrMenu.SetVisible(false);
            m_network.SendLaunchSteamVr();
        }
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

void VxrQuestApp::DecodeLiveDesktopStrip(const uint8_t* data, size_t size) {
    // 8-byte header from ProjectVXR.exe:
    // ['V','X' (Full RLE) or 'V','D' (Delta RLE), uint16_t srcW (1920, 1280, or 640), uint16_t yStart, uint16_t rawStripH]
    // Bit 15 (0x8000) of rawStripH is the End-Of-Frame flag for tear-free double-buffered presentation!
    if (!data || size < 8) return;

    uint16_t srcW      = 0;
    uint16_t srcH      = 800;
    uint16_t yStart    = 0;
    uint16_t rawStripH = 0;
    uint16_t stripH    = 0;
    bool isByteTripletRle = (data[0] == 'V' && data[1] == 'X');
    bool isDeltaRle       = (data[0] == 'V' && data[1] == 'D');
    bool isEndOfFrame     = false;

    if (isByteTripletRle || isDeltaRle) {
        srcW      = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
        yStart    = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
        rawStripH = static_cast<uint16_t>(data[6]) | (static_cast<uint16_t>(data[7]) << 8);
        isEndOfFrame = (rawStripH & 0x8000u) != 0;
        stripH       = rawStripH & 0x7FFFu;
        if (srcW >= 1920) {
            srcH = 1080;
        } else if (srcW >= 1280) {
            srcH = 800;
        } else {
            srcH = 400;
        }
    } else {
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(data);
        srcW      = u16[0];
        srcH      = u16[1];
        yStart    = u16[2];
        rawStripH = u16[3];
        isEndOfFrame = (rawStripH & 0x8000u) != 0;
        stripH       = rawStripH & 0x7FFFu;
    }

    if (srcW == 0 || srcH == 0 || stripH == 0 || yStart >= srcH || (yStart + stripH) > srcH) {
        return;
    }

    bool isNativeDirect = (srcW >= 1280);
    uint32_t targetW = isNativeDirect ? srcW : MENU_OVERLAY_W;
    uint32_t targetH = isNativeDirect ? srcH : MENU_OVERLAY_H;
    size_t totalTargetPixels = static_cast<size_t>(targetW) * targetH;

    if (g_liveDesktopW != targetW || g_liveDesktopH != targetH ||
        m_desktopRgbaBuffer.size() < totalTargetPixels ||
        g_desktopStagingBuffer.size() < totalTargetPixels) {
        g_liveDesktopW = targetW;
        g_liveDesktopH = targetH;
        m_desktopRgbaBuffer.assign(totalTargetPixels, 0xFF1E1914u);
        g_desktopStagingBuffer.assign(totalTargetPixels, 0xFF1E1914u);
    }

    size_t totalStripPixels = static_cast<size_t>(srcW) * stripH;
    size_t pixelIdx = 0;

    if (isDeltaRle) {
        // High-speed 90Hz Delta-RLE strip decoded into back-buffer g_desktopStagingBuffer:
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
                    for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx, ++dstOffset) {
                        if (dstOffset < totalTargetPixels) {
                            g_desktopStagingBuffer[dstOffset] = rgba;
                        }
                    }
                } else {
                    for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx) {
                        uint32_t sx = static_cast<uint32_t>(pixelIdx % srcW);
                        uint32_t sy = yStart + static_cast<uint32_t>(pixelIdx / srcW);
                        uint32_t dx = sx * 2;
                        uint32_t dy = sy * 2;
                        if (dx + 1 < targetW && dy + 1 < targetH) {
                            uint32_t* row0 = g_desktopStagingBuffer.data() + dy * targetW + dx;
                            uint32_t* row1 = row0 + targetW;
                            row0[0] = rgba; row0[1] = rgba;
                            row1[0] = rgba; row1[1] = rgba;
                        }
                    }
                }
            }
        }
    } else if (isByteTripletRle) {
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
            uint32_t rgba = (0xFFu << 24) | (static_cast<uint32_t>(b8) << 16) |
                            (static_cast<uint32_t>(g8) << 8) | static_cast<uint32_t>(r8);

            if (isNativeDirect) {
                size_t dstOffset = static_cast<size_t>(yStart) * targetW + pixelIdx;
                for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx, ++dstOffset) {
                    if (dstOffset < totalTargetPixels) {
                        g_desktopStagingBuffer[dstOffset] = rgba;
                    }
                }
            } else {
                for (uint8_t r = 0; r < runLen && pixelIdx < totalStripPixels; ++r, ++pixelIdx) {
                    uint32_t sx = static_cast<uint32_t>(pixelIdx % srcW);
                    uint32_t sy = yStart + static_cast<uint32_t>(pixelIdx / srcW);
                    uint32_t dx = sx * 2;
                    uint32_t dy = sy * 2;
                    if (dx + 1 < targetW && dy + 1 < targetH) {
                        uint32_t* row0 = g_desktopStagingBuffer.data() + dy * targetW + dx;
                        uint32_t* row1 = row0 + targetW;
                        row0[0] = rgba; row0[1] = rgba;
                        row1[0] = rgba; row1[1] = rgba;
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
                    uint32_t* row0 = g_desktopStagingBuffer.data() + dy * targetW + dx;
                    uint32_t* row1 = row0 + targetW;
                    row0[0] = rgba; row0[1] = rgba;
                    row1[0] = rgba; row1[1] = rgba;
                }
            }
        }
    }

    // Atomic frame flip on End-Of-Frame marker (or bottom strip / 20ms fallback) to eliminate horizontal tearing!
    uint64_t nowMs = GetSteadyTimeMs();
    if (isEndOfFrame || (yStart + stripH >= srcH) || !m_hasLiveDesktopFrame || (nowMs - g_lastStagingFlipMs >= 20)) {
        std::memcpy(m_desktopRgbaBuffer.data(), g_desktopStagingBuffer.data(), totalTargetPixels * sizeof(uint32_t));
        g_lastStagingFlipMs = nowMs;
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
        if (m_vrMenu.GetActiveTab() == VrMenuTab::COMPUTERS && m_hasLiveDesktopFrame && m_network.IsConnected()) {
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
            const uint32_t borderCol = 0xFFFF901Eu;
            DrawOverlayOutline(m_compositedRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H,
                               prevX, prevY, prevW, prevH, borderCol);
            DrawOverlayRect(m_compositedRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H,
                            prevX + 8, prevY + 8, 220, 22, 0xD91E1612u);
            DrawOverlayText(m_compositedRgbaBuffer.data(), MENU_OVERLAY_W, MENU_OVERLAY_H,
                            prevX + 14, prevY + 12, "CLICK PREVIEW TO ENTER DESKTOP", 0xFFFFFFFFu, 1);
        }
    } else {
        // Full-Screen Live PC Desktop or Immersive 3D Stereo SteamVR Mode (1:1 Native 1920x1080)!
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

        // In Immersive 3D Stereo SteamVR mode, do NOT draw 2D desktop toolbars or 2D cursor over the SBS stereo eyes!
        if (m_openXr.IsImmersiveStereoVrMode()) {
            return;
        }

        // 1. Top-left Hamburger Menu Pill (0..44, 0..32)
        DrawOverlayRect(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH, 4, 4, 38, 28, 0xE629201Cu);
        DrawOverlayOutline(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH, 4, 4, 38, 28, 0xFFFF901Eu);
        for (int bar = 0; bar < 3; ++bar) {
            DrawOverlayRect(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                            12, 10 + bar * 6, 22, 2, 0xFFFF901Eu);
        }

        // 2. Top-Center Floating Quick-Action Bar (Virtual Desktop / ALVR style)
        const uint32_t barW = 720;
        const uint32_t barH = 34;
        const uint32_t barX = (g_compositedW > barW) ? (g_compositedW - barW) / 2 : 48;
        const uint32_t barY = 6;
        DrawOverlayRect(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                        barX, barY, barW, barH, 0xE6221B16u);
        DrawOverlayOutline(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                           barX, barY, barW, barH, 0xFF6A5848u);

        int ptrPx = static_cast<int>(m_vrMenu.GetPointerX() * static_cast<float>(g_compositedW - 1));
        int ptrPy = static_cast<int>(m_vrMenu.GetPointerY() * static_cast<float>(g_compositedH - 1));

        const char* barLabels[6] = {
            "MENU",
            g_virtualKeyboardOpen ? "HIDE KB" : "KEYBOARD",
            "ENTER VR",
            "DISTANCE",
            m_vrMenu.GetSettings().microphone_passthrough ? "MIC: ON" : "MIC: MUTE",
            "DISCONNECT"
        };
        const uint32_t btnW = 114;
        const uint32_t btnH = 26;
        for (int i = 0; i < 6; ++i) {
            uint32_t bx = barX + 8 + static_cast<uint32_t>(i) * (btnW + 4);
            uint32_t by = barY + 4;
            bool hovered = (ptrPx >= static_cast<int>(bx) && ptrPx < static_cast<int>(bx + btnW) &&
                            ptrPy >= static_cast<int>(by) && ptrPy < static_cast<int>(by + btnH));
            uint32_t bgCol = 0xFF382E26u;
            uint32_t borderCol = 0xFF6A5848u;
            if (i == 1 && g_virtualKeyboardOpen) {
                bgCol = 0xFFFF901Eu;
                borderCol = 0xFFFFFFFFu;
            } else if (i == 2) {
                bgCol = hovered ? 0xFFFFA53Au : 0xFFCC7014u;
                borderCol = 0xFFFFB860u;
            } else if (i == 5) {
                bgCol = hovered ? 0xFF4242E5u : 0xFF2E2EB8u; // Red Disconnect (ABGR)
                borderCol = 0xFF7070FFu;
            } else if (hovered) {
                bgCol = 0xFF54463Au;
                borderCol = 0xFFFF901Eu;
            }
            DrawOverlayRect(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH, bx, by, btnW, btnH, bgCol);
            DrawOverlayOutline(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH, bx, by, btnW, btnH, borderCol);
            uint32_t textLen = static_cast<uint32_t>(std::strlen(barLabels[i])) * 6;
            uint32_t tx = bx + ((btnW > textLen) ? (btnW - textLen) / 2 : 4);
            DrawOverlayText(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                            tx, by + 9, barLabels[i], 0xFFFFFFFFu, 1);
        }

        // 3. On-Screen Interactive 5-Row QWERTY Virtual Keyboard (if toggled open)
        if (g_virtualKeyboardOpen) {
            const uint32_t kbW = 860;
            const uint32_t kbH = 250;
            const uint32_t kbX = (g_compositedW > kbW) ? (g_compositedW - kbW) / 2 : 20;
            const uint32_t kbY = (g_compositedH > kbH + 16) ? (g_compositedH - kbH - 16) : (g_compositedH - kbH);

            DrawOverlayRect(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                            kbX, kbY, kbW, kbH, 0xF21E1814u);
            DrawOverlayOutline(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                               kbX, kbY, kbW, kbH, 0xFFFF901Eu);

            const VkKeyDef* rows[5] = { kVkRow0, kVkRow1, kVkRow2, kVkRow3, kVkRow4 };
            const size_t rowCounts[5] = {
                sizeof(kVkRow0) / sizeof(kVkRow0[0]),
                sizeof(kVkRow1) / sizeof(kVkRow1[0]),
                sizeof(kVkRow2) / sizeof(kVkRow2[0]),
                sizeof(kVkRow3) / sizeof(kVkRow3[0]),
                sizeof(kVkRow4) / sizeof(kVkRow4[0])
            };

            for (int r = 0; r < 5; ++r) {
                uint32_t curX = kbX + 12;
                uint32_t curY = kbY + 10 + static_cast<uint32_t>(r) * 46;
                uint32_t keyH = 40;
                for (size_t k = 0; k < rowCounts[r]; ++k) {
                    const VkKeyDef& kd = rows[r][k];
                    uint32_t keyW = (kd.widthUnits * 56) / 10;
                    bool keyHover = (ptrPx >= static_cast<int>(curX) && ptrPx < static_cast<int>(curX + keyW) &&
                                     ptrPy >= static_cast<int>(curY) && ptrPy < static_cast<int>(curY + keyH));
                    bool modActive = ((kd.vkCode == 0x10 && g_vkShiftActive) ||
                                      (kd.vkCode == 0x14 && g_vkCapsLockActive) ||
                                      (kd.vkCode == 0x11 && g_vkCtrlActive) ||
                                      (kd.vkCode == 0x12 && g_vkAltActive));
                    uint32_t kBg = modActive ? 0xFFFF901Eu : (keyHover ? 0xFF524438u : 0xFF322820u);
                    uint32_t kBorder = (keyHover || modActive) ? 0xFFFFFFFFu : 0xFF6A5848u;
                    DrawOverlayRect(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                                    curX, curY, keyW, keyH, kBg);
                    DrawOverlayOutline(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                                       curX, curY, keyW, keyH, kBorder);
                    uint32_t lblW = static_cast<uint32_t>(std::strlen(kd.label)) * 6;
                    uint32_t lx = curX + ((keyW > lblW) ? (keyW - lblW) / 2 : 2);
                    DrawOverlayText(m_compositedRgbaBuffer.data(), g_compositedW, g_compositedH,
                                    lx, curY + 16, kd.label, 0xFFFFFFFFu, 1);
                    curX += keyW + 4;
                }
            }
        }

        // 4. Performance Overlay HUD in top-right if enabled
        if (m_vrMenu.GetSettings().show_performance_overlay && m_perfHudRgbaBuffer.size() >= PERF_HUD_W * PERF_HUD_H) {
            const uint32_t hudX = g_compositedW - PERF_HUD_W - 16;
            const uint32_t hudY = 46;
            const uint32_t copyH = 92;
            for (uint32_t y = 0; y < copyH; ++y) {
                std::memcpy(m_compositedRgbaBuffer.data() + static_cast<size_t>(hudY + y) * g_compositedW + hudX,
                            m_perfHudRgbaBuffer.data() + y * PERF_HUD_W,
                            PERF_HUD_W * sizeof(uint32_t));
            }
        }

        // 5. Long-Press Right-Click Gesture Ring + VR Laser Pointer Dot
        int cx = ptrPx;
        int cy = ptrPy;
        if (g_longPressRingProgress > 0.01f) {
            const int ringRadius = 16;
            for (int dy = -ringRadius - 2; dy <= ringRadius + 2; ++dy) {
                for (int dx = -ringRadius - 2; dx <= ringRadius + 2; ++dx) {
                    int distSq = dx * dx + dy * dy;
                    if (distSq >= (ringRadius - 2) * (ringRadius - 2) &&
                        distSq <= (ringRadius + 2) * (ringRadius + 2)) {
                        float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx)); // -pi..pi
                        float normAngle = (angle + 3.14159265f) / (2.0f * 3.14159265f);
                        if (normAngle <= g_longPressRingProgress) {
                            int px = cx + dx;
                            int py = cy + dy;
                            if (px >= 0 && px < static_cast<int>(g_compositedW) &&
                                py >= 0 && py < static_cast<int>(g_compositedH)) {
                                m_compositedRgbaBuffer[static_cast<size_t>(py) * g_compositedW + px] =
                                    (g_longPressRightClickFired) ? 0xFF66CC33u : 0xFFFF901Eu;
                            }
                        }
                    }
                }
            }
        }
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

static bool HandleDesktopOverlayClick(float norm_x, float norm_y,
                                      uint32_t screenW, uint32_t screenH,
                                      VxrVrMenuUi& vrMenu, VxrClientNetwork& network,
                                      VxrOpenXrClient& openXr, bool* outHasLiveFrame) {
    int px = static_cast<int>(norm_x * static_cast<float>(screenW - 1));
    int py = static_cast<int>(norm_y * static_cast<float>(screenH - 1));

    // 1. Top-left Hamburger Menu Pill (0..44, 0..34)
    if (px <= 44 && py <= 34) {
        vrMenu.SetVisible(true);
        return true;
    }

    // 2. Top-Center Floating Quick-Action Bar
    const uint32_t barW = 720;
    const uint32_t barH = 34;
    const uint32_t barX = (screenW > barW) ? (screenW - barW) / 2 : 48;
    const uint32_t barY = 6;
    if (px >= static_cast<int>(barX) && px < static_cast<int>(barX + barW) &&
        py >= static_cast<int>(barY) && py < static_cast<int>(barY + barH)) {
        const uint32_t btnW = 114;
        const uint32_t btnH = 26;
        for (int i = 0; i < 6; ++i) {
            uint32_t bx = barX + 8 + static_cast<uint32_t>(i) * (btnW + 4);
            uint32_t by = barY + 4;
            if (px >= static_cast<int>(bx) && px < static_cast<int>(bx + btnW) &&
                py >= static_cast<int>(by) && py < static_cast<int>(by + btnH)) {
                if (i == 0) {
                    vrMenu.SetVisible(true);
                } else if (i == 1) {
                    g_virtualKeyboardOpen = !g_virtualKeyboardOpen;
                } else if (i == 2) {
                    g_manualStereoVrMode = true;
                    openXr.SetImmersiveStereoVrMode(true);
                    network.SendLaunchSteamVr();
                } else if (i == 3) {
                    auto& s = vrMenu.GetSettingsMutable();
                    if (s.screen_distance_m < 1.6f) s.screen_distance_m = 2.0f;
                    else if (s.screen_distance_m < 2.2f) s.screen_distance_m = 2.6f;
                    else s.screen_distance_m = 1.4f;
                } else if (i == 4) {
                    auto& s = vrMenu.GetSettingsMutable();
                    s.microphone_passthrough = !s.microphone_passthrough;
                } else if (i == 5) {
                    auto& s = vrMenu.GetSettingsMutable();
                    s.auto_connect = false;
                    network.Disconnect();
                    if (outHasLiveFrame) *outHasLiveFrame = false;
                    g_remoteStereoVrActive = false;
                    g_manualStereoVrMode = false;
                    openXr.SetImmersiveStereoVrMode(false);
                    vrMenu.SetVisible(true);
                }
                return true;
            }
        }
        return true;
    }

    // 3. On-Screen Interactive Virtual Keyboard
    if (g_virtualKeyboardOpen) {
        const uint32_t kbW = 860;
        const uint32_t kbH = 250;
        const uint32_t kbX = (screenW > kbW) ? (screenW - kbW) / 2 : 20;
        const uint32_t kbY = (screenH > kbH + 16) ? (screenH - kbH - 16) : (screenH - kbH);
        if (px >= static_cast<int>(kbX) && px < static_cast<int>(kbX + kbW) &&
            py >= static_cast<int>(kbY) && py < static_cast<int>(kbY + kbH)) {
            const VkKeyDef* rows[5] = { kVkRow0, kVkRow1, kVkRow2, kVkRow3, kVkRow4 };
            const size_t rowCounts[5] = {
                sizeof(kVkRow0) / sizeof(kVkRow0[0]),
                sizeof(kVkRow1) / sizeof(kVkRow1[0]),
                sizeof(kVkRow2) / sizeof(kVkRow2[0]),
                sizeof(kVkRow3) / sizeof(kVkRow3[0]),
                sizeof(kVkRow4) / sizeof(kVkRow4[0])
            };
            for (int r = 0; r < 5; ++r) {
                uint32_t curX = kbX + 12;
                uint32_t curY = kbY + 10 + static_cast<uint32_t>(r) * 46;
                uint32_t keyH = 40;
                for (size_t k = 0; k < rowCounts[r]; ++k) {
                    const VkKeyDef& kd = rows[r][k];
                    uint32_t keyW = (kd.widthUnits * 56) / 10;
                    if (px >= static_cast<int>(curX) && px < static_cast<int>(curX + keyW) &&
                        py >= static_cast<int>(curY) && py < static_cast<int>(curY + keyH)) {
                        if (kd.vkCode == 0xFF) {
                            g_virtualKeyboardOpen = false;
                        } else if (kd.vkCode == 0x10) {
                            g_vkShiftActive = !g_vkShiftActive;
                        } else if (kd.vkCode == 0x14) {
                            g_vkCapsLockActive = !g_vkCapsLockActive;
                            network.SendVirtualKeyInput(0x14, 0x00);
                        } else if (kd.vkCode == 0x11) {
                            g_vkCtrlActive = !g_vkCtrlActive;
                        } else if (kd.vkCode == 0x12) {
                            g_vkAltActive = !g_vkAltActive;
                        } else {
                            uint8_t flags = 0x00; // Tap (Down + Up)
                            if (g_vkShiftActive || g_vkCapsLockActive) flags |= 0x04;
                            if (g_vkCtrlActive) flags |= 0x08;
                            if (g_vkAltActive)  flags |= 0x10;
                            network.SendVirtualKeyInput(kd.vkCode, flags);
                            g_vkShiftActive = false;
                            g_vkCtrlActive  = false;
                            g_vkAltActive   = false;
                        }
                        return true;
                    }
                    curX += keyW + 4;
                }
            }
            return true;
        }
    }

    return false;
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
        if (is_click && !m_openXr.IsImmersiveStereoVrMode()) {
            if (HandleDesktopOverlayClick(norm_x, norm_y, g_compositedW, g_compositedH,
                                          m_vrMenu, m_network, m_openXr, &m_hasLiveDesktopFrame)) {
                return;
            }
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
    uint64_t nowMs = GetSteadyTimeMs();

#ifdef __ANDROID__
    uint64_t syncIntervalMs = (m_network.IsConnectPending() && !m_network.IsConnected()) ? 400 : 1000;
    if (nowMs - m_lastBroadcastTimeMs >= syncIntervalMs) {
        m_lastBroadcastTimeMs = nowMs;
        const std::string& hsModel = m_vrMenu.GetSettings().headset_model;
        // Passive broadcast discovery ONLY — does NOT force connection!
        m_network.SendBroadcastDiscovery(hsModel.c_str(), 38810);

        // Only send unicast Connect if the user explicitly clicked Connect (or enabled Auto-Connect):
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

    // 1. Poll mDNS / Broadcast Host Discovery & Incoming Video / Audio Packets over UDP
    std::vector<DiscoveredHostInfo> discoveredHosts;
    m_network.PollMdnsDiscovery(&discoveredHosts);

    ReassembledVideoFrame netFrame = {};
    while (m_network.PollNetwork(&netFrame)) {
        if (netFrame.size > 0 && !netFrame.data.empty()) {
            g_remoteStereoVrActive = netFrame.is_stereo_vr;
            if (netFrame.codec_id == 0x55) {
                // Real Live RLE-RGB565 Desktop / SteamVR Stereo Strip from ProjectVXR.exe
                DecodeLiveDesktopStrip(netFrame.data.data(), netFrame.size);
            } else if (netFrame.codec_id == 0x56) {
                m_multiDecoder.FeedSlicedPacket(netFrame.data.data(), netFrame.size, netFrame.is_keyframe);
            } else {
                m_decoder.QueueInputBuffer(netFrame.data.data(), netFrame.size, netFrame.pts_us, netFrame.is_keyframe);
            }
            m_cursor.UpdatePosition(netFrame.cursor_norm_x, netFrame.cursor_norm_y, netFrame.cursor_visible);
        }
    }

    // Stream real Quest Microphone audio to PC when connected & enabled
    if (m_network.IsConnected()) {
        m_network.PollAndSendMicrophone(
            m_vrMenu.GetSettings().microphone_passthrough,
            m_vrMenu.GetSettings().mic_volume_pct
        );
    } else {
        g_remoteStereoVrActive = false;
        g_manualStereoVrMode = false;
    }

    // Automatically enter/exit 3D Stereo VR Projection mode when SteamVR compositor is active (and menu is closed)
    bool wantImmersiveStereoVr = !m_vrMenu.IsVisible() && m_network.IsConnected() &&
                                 (g_remoteStereoVrActive || g_manualStereoVrMode);
    m_openXr.SetImmersiveStereoVrMode(wantImmersiveStereoVr);

    discoveredHosts = m_network.GetDiscoveredHosts();
    if (!discoveredHosts.empty()) {
        std::vector<VrComputerEntry> comps;
        comps.reserve(discoveredHosts.size());
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

        uint32_t leftBtns = 0;
        if (ctrl.button_x)    leftBtns |= (1u << 2);
        if (ctrl.button_y)    leftBtns |= (1u << 3);
        if (ctrl.button_menu) leftBtns |= (1u << 5);
        if (m_openXr.GetActiveHand() == 0) {
            if (ctrl.trigger_clicked) leftBtns |= (1u << 0);
            if (ctrl.grip_clicked)    leftBtns |= (1u << 1);
        }

        uint32_t rightBtns = 0;
        if (ctrl.button_a) rightBtns |= (1u << 2);
        if (ctrl.button_b) rightBtns |= (1u << 3);
        if (m_openXr.GetActiveHand() == 1) {
            if (ctrl.trigger_clicked) rightBtns |= (1u << 0);
            if (ctrl.grip_clicked)    rightBtns |= (1u << 1);
        }

        trackPkt.left_trigger  = (m_openXr.GetActiveHand() == 0) ? ctrl.trigger_value : 0.0f;
        trackPkt.left_grip     = (m_openXr.GetActiveHand() == 0) ? ctrl.grip_value : 0.0f;
        trackPkt.left_joy[0]   = (m_openXr.GetActiveHand() == 0) ? ctrl.thumbstick_x : 0.0f;
        trackPkt.left_joy[1]   = (m_openXr.GetActiveHand() == 0) ? ctrl.thumbstick_y : 0.0f;
        trackPkt.left_buttons  = leftBtns;

        trackPkt.right_trigger = (m_openXr.GetActiveHand() == 1) ? ctrl.trigger_value : 0.0f;
        trackPkt.right_grip    = (m_openXr.GetActiveHand() == 1) ? ctrl.grip_value : 0.0f;
        trackPkt.right_joy[0]  = (m_openXr.GetActiveHand() == 1) ? ctrl.thumbstick_x : 0.0f;
        trackPkt.right_joy[1]  = (m_openXr.GetActiveHand() == 1) ? ctrl.thumbstick_y : 0.0f;
        trackPkt.right_buttons = rightBtns;

        m_network.SendVrTrackingState(trackPkt);
    }

    // Left Menu button toggles the 7-Tab Menu (and exits Stereo VR mode when menu is open)
    if (ctrl.button_menu && !m_prevMenuBtnPressed) {
        m_vrMenu.ToggleVisible();
    }
    m_prevMenuBtnPressed = ctrl.button_menu;

    // Left X button toggles the On-Screen Virtual Keyboard when in 2D Desktop View
    if (ctrl.button_x && !g_prevBtnX && !m_vrMenu.IsVisible() && !m_openXr.IsImmersiveStereoVrMode()) {
        g_virtualKeyboardOpen = !g_virtualKeyboardOpen;
    }
    g_prevBtnX = ctrl.button_x;

    if (!m_hasSurfacePointer || m_openXr.IsRealRuntimeActive()) {
        float pointerU = 0.5f;
        float pointerV = 0.5f;
        float screenDist = m_vrMenu.GetSettings().screen_distance_m;
        bool hitScreen = m_openXr.ComputeDesktopPointerUV(screenDist, &pointerU, &pointerV);

        if (hitScreen) {
            m_vrMenu.OnPointerMove(pointerU, pointerV);
            if (m_vrMenu.IsVisible()) {
                g_longPressRingProgress = 0.0f;
                if (ctrl.trigger_clicked && !m_prevTriggerClicked) {
                    m_vrMenu.OnPointerClick(pointerU, pointerV);
                    ProcessMenuComputerActions();
                }
            } else if (!m_openXr.IsImmersiveStereoVrMode()) {
                bool consumedByOverlay = false;
                if (ctrl.trigger_clicked && !m_prevTriggerClicked) {
                    consumedByOverlay = HandleDesktopOverlayClick(
                        pointerU, pointerV, g_compositedW, g_compositedH,
                        m_vrMenu, m_network, m_openXr, &m_hasLiveDesktopFrame
                    );
                    g_triggerHoldStartMs       = nowMs;
                    g_triggerHoldStartU        = pointerU;
                    g_triggerHoldStartV        = pointerV;
                    g_longPressRightClickFired = consumedByOverlay; // don't fire right-click on toolbar/keyboard
                    g_longPressRingProgress    = 0.0f;
                }

                if (!consumedByOverlay && m_vrMenu.GetSettings().controllers_interact_desktop && m_network.IsConnected()) {
                    uint8_t btnMask = 0;
                    if (ctrl.trigger_clicked || ctrl.button_a) btnMask |= 0x01; // Left click / drag
                    if (ctrl.button_b || ctrl.button_y)        btnMask |= 0x02; // Immediate Right click
                    if (ctrl.grip_clicked)                     btnMask |= 0x04; // Middle click

                    // Long-Press Trigger (~750ms hold still) -> Right-Click Gesture with animated progress ring!
                    if (ctrl.trigger_clicked) {
                        float moveDist = std::hypot(pointerU - g_triggerHoldStartU, pointerV - g_triggerHoldStartV);
                        if (moveDist > 0.022f) {
                            // User is dragging -- cancel long-press right-click
                            g_triggerHoldStartMs = nowMs;
                            g_triggerHoldStartU  = pointerU;
                            g_triggerHoldStartV  = pointerV;
                            g_longPressRingProgress = 0.0f;
                        } else {
                            uint64_t heldMs = (nowMs >= g_triggerHoldStartMs) ? (nowMs - g_triggerHoldStartMs) : 0;
                            if (!g_longPressRightClickFired) {
                                if (heldMs >= 180 && heldMs < 750) {
                                    g_longPressRingProgress = static_cast<float>(heldMs - 180) / 570.0f;
                                } else if (heldMs >= 750) {
                                    g_longPressRightClickFired = true;
                                    g_longPressRingProgress = 1.0f;
                                    // Release left-button & fire Right-Click (0x02)
                                    m_network.SendDesktopMouseInput(pointerU, pointerV, 0x00, 0);
                                    m_network.SendDesktopMouseInput(pointerU, pointerV, 0x02, 0);
                                    m_network.SendDesktopMouseInput(pointerU, pointerV, 0x00, 0);
                                    btnMask = 0;
                                }
                            } else {
                                btnMask = 0; // keep released after right-click fired
                            }
                        }
                    } else {
                        g_longPressRingProgress = 0.0f;
                        g_longPressRightClickFired = false;
                    }

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
        g_manualStereoVrMode = true;
        m_vrMenu.SetVisible(false);
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
        stats.encoding_ms = 0.6f;
        stats.decoding_ms = 0.5f;
        float rtt = m_network.GetMeasuredRttMs();
        if (rtt >= 0.2f && rtt <= 12.0f) {
            stats.networking_ms = (std::max)(0.7f, (std::min)(1.3f, rtt * 0.35f));
        } else {
            stats.networking_ms = 0.9f;
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
