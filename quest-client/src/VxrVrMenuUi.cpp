#include "VxrVrMenuUi.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace vxr {

namespace {

// Color Palette: Little-Endian RGBA_8888 (bytes in memory: [R, G, B, A] -> uint32_t 0xAABBGGRR)
constexpr uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFFu) {
    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

constexpr uint32_t COLOR_BG_OUTER        = Rgba(0x10, 0x12, 0x16);
constexpr uint32_t COLOR_BG_SIDEBAR      = Rgba(0x16, 0x19, 0x20);
constexpr uint32_t COLOR_BG_PANEL        = Rgba(0x1C, 0x20, 0x29);
constexpr uint32_t COLOR_BG_CARD         = Rgba(0x24, 0x29, 0x36);
constexpr uint32_t COLOR_BG_TOPBAR       = Rgba(0x12, 0x15, 0x1C);
constexpr uint32_t COLOR_BG_BOTTOMBAR    = Rgba(0x12, 0x15, 0x1C);
constexpr uint32_t COLOR_BORDER          = Rgba(0x32, 0x39, 0x4A);
constexpr uint32_t COLOR_ACCENT_BLUE     = Rgba(0x1E, 0x90, 0xFF); // #1E90FF DodgerBlue
constexpr uint32_t COLOR_ACCENT_HOVER    = Rgba(0x3C, 0xA0, 0xFF);
constexpr uint32_t COLOR_TEXT_PRIMARY    = Rgba(0xF2, 0xF5, 0xFA);
constexpr uint32_t COLOR_TEXT_SECONDARY  = Rgba(0xA5, 0xB0, 0xC2);
constexpr uint32_t COLOR_TEXT_MUTED      = Rgba(0x6E, 0x78, 0x8A);
constexpr uint32_t COLOR_STATUS_GREEN    = Rgba(0x2E, 0xCC, 0x71);
constexpr uint32_t COLOR_STATUS_ORANGE   = Rgba(0xF3, 0x9C, 0x12);
constexpr uint32_t COLOR_DANGER_RED      = Rgba(0xE7, 0x4C, 0x3C);
constexpr uint32_t COLOR_CONTROL_BG      = Rgba(0x2B, 0x31, 0x40);

// HitBox Action IDs
enum HitActionId : uint32_t {
    HIT_NONE = 0,
    // Sidebar Tabs
    HIT_TAB_COMPUTERS    = 10,
    HIT_TAB_ENVIRONMENTS = 11,
    HIT_TAB_GAMES        = 12,
    HIT_TAB_INPUT        = 13,
    HIT_TAB_SETTINGS     = 14,
    HIT_TAB_STREAMING    = 15,
    HIT_TAB_VIDEOS       = 16,
    HIT_BTN_STEAMVR      = 17,
    HIT_BTN_QUIT         = 18,
    HIT_BTN_VIEW_DESKTOP = 19,

    // COMPUTERS Tab
    HIT_COMPUTER_TOGGLE_CONNECT = 100,

    // ENVIRONMENTS Tab
    HIT_ENV_SELECT          = 200,
    HIT_ENV_SLIDER_DISTANCE = 201,
    HIT_ENV_SLIDER_SIZE     = 202,
    HIT_ENV_SLIDER_CURVE    = 203,

    // GAMES Tab
    HIT_GAME_LAUNCH = 300,

    // INPUT Tab
    HIT_IN_CONTROLLERS_INTERACT   = 400,
    HIT_IN_HANDS_INTERACT         = 401,
    HIT_IN_POINTER_STABILIZATION  = 402,
    HIT_IN_AUTO_HIDE_CONTROLLERS  = 403,
    HIT_IN_THUMB_VERT_SCROLL      = 404,
    HIT_IN_THUMB_HORIZ_SCROLL     = 405,
    HIT_IN_HAND_TRACKING          = 406,
    HIT_IN_GRIP_GRAB_RESIZE       = 407,
    HIT_IN_HOLD_MENU_SWITCH       = 408,
    HIT_IN_EMULATE_GAMEPAD        = 409,
    HIT_IN_TOUCH_AS_GAMEPAD       = 410,
    HIT_IN_DPAD_RIGHT_GRIP        = 411,
    HIT_IN_DPAD_RIGHT_THUMB       = 412,
    HIT_IN_DPAD_RIGHT_THUMBREST   = 413,
    HIT_IN_CREATE_DESK_PORTAL     = 414,

    // SETTINGS Tab
    HIT_SET_AUTO_CONNECT          = 500,
    HIT_SET_SHOW_GAMES            = 501,
    HIT_SET_OPTIMAL_RES           = 502,
    HIT_SET_ARRANGE_ON_RECENTER   = 503,
    HIT_SET_ARRANGE_NOW           = 504,
    HIT_SET_ENV_QUALITY           = 505,
    HIT_SET_FRAME_RATE            = 506,
    HIT_SET_DESKTOP_BITRATE       = 507,
    HIT_SET_CONFIG_TRANSPARENCY   = 508,
    HIT_SET_BRIGHTNESS            = 509,
    HIT_SET_DYNAMIC_LIGHTING      = 510,
    HIT_SET_BG_MUSIC              = 511,
    HIT_SET_MIC_PASSTHROUGH       = 512,
    HIT_SET_MIC_VOLUME            = 513,
    HIT_SET_NOISE_CANCEL          = 514,
    HIT_SET_CUSTOM_ORIENTATION    = 515,
    HIT_SET_BOOST_CLOCKS          = 516,
    HIT_SET_COPY_SCREENSHOTS      = 517,
    HIT_SET_COLOR_VIBRANCE        = 518,
    HIT_SET_REMOVE_HEAD_DELAY     = 519,

    // STREAMING Tab
    HIT_STR_VR_QUALITY            = 600,
    HIT_STR_VR_FPS                = 601,
    HIT_STR_VR_BITRATE            = 602,
    HIT_STR_SHARPENING            = 603,
    HIT_STR_PASS_ENV              = 604,
    HIT_STR_PASS_HANDS            = 605,
    HIT_STR_PASS_PORTAL           = 606,
    HIT_STR_CONFIG_PASSTHROUGH    = 607,
    HIT_STR_GAMMA                 = 608,
    HIT_STR_SSW_MODE              = 609,
    HIT_STR_SGSR                  = 610,
    HIT_STR_VIDEO_BUFFERING       = 611,
    HIT_STR_CENTER_PLAY_SPACE     = 612,
    HIT_STR_TRACK_CONTROLLERS     = 613,
    HIT_STR_FORWARD_TRACKING      = 614,
    HIT_STR_EMULATE_VIVE          = 615,
    HIT_STR_EMULATE_INDEX         = 616,
    HIT_STR_COLOR_VIBRANCE        = 617,
    HIT_STR_PERF_OVERLAY          = 618,
    HIT_STR_RESET_DEFAULTS        = 619,

    // VIDEOS Tab
    HIT_VID_SELECT_FILE           = 700,
    HIT_VID_SELECT_PROJECTION     = 701
};

// 5x7 ASCII Bitmap Font for printable characters 32 (' ') to 126 ('~')
// Each character has 5 columns; each column is a 7-bit mask (LSB = top row, bit 6 = bottom row).
static const uint8_t kFont5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
    {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    {0x00, 0x01, 0x02, 0x04, 0x00}, // '`'
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 'a'
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 'c'
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 'f'
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 'p'
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 's'
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
    {0x00, 0x00, 0x7F, 0x00, 0x00}, // '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
    {0x08, 0x04, 0x08, 0x10, 0x08}  // '~'
};

static const uint32_t kSupportedFps[7] = { 60, 72, 80, 90, 96, 100, 120 };

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

} // anonymous namespace

// ============================================================================
// VrMenuSettings Implementation
// ============================================================================

void VrMenuSettings::ResetToDefaults() {
    selected_environment = 0;
    screen_distance_m = 2.2f;
    screen_size_scale = 1.25f;
    screen_curvature = 0.35f;

    // INPUT
    controllers_interact_desktop = true;
    hands_interact_desktop = true;
    pointer_stabilization = true;
    auto_hide_controllers = true;
    thumbstick_vertical_scroll = true;
    thumbstick_horizontal_scroll = true;
    hand_tracking = true;
    press_grip_to_grab_resize = true;
    hold_menu_to_switch_out_vr = true;

    emulate_gamepad_on_pc = true;
    use_touch_controllers_as_gamepad = false;

    dpad_when_right_grip_pressed = false;
    dpad_when_right_thumbstick_pressed = false;
    dpad_when_right_thumbrest_active = false;
    create_desk_portal_requested = false;

    // SETTINGS
    auto_connect = true;
    show_games_on_connect = false;
    use_optimal_resolution = true;
    arrange_monitors_on_recenter = false;
    arrange_monitors_requested = false;
    environment_quality = 0; // Low
    desktop_frame_rate = 90;
    desktop_bitrate_mbps = 120;
    configure_transparency_requested = false;
    screen_brightness_pct = 100;
    dynamic_lighting = 2; // Always enabled
    bg_music_when_disconnected = false;
    microphone_passthrough = true;
    mic_volume_pct = 71;
    noise_cancellation = true;
    allow_custom_orientation = true;
    boost_clock_rates = true;
    copy_screenshots_to_desktop = true;
    increase_color_vibrance = true;
    remove_head_lock_delay = true;

    // STREAMING
    ResetStreamingToDefaults();

    settingsDirty = true;
}

void VrMenuSettings::ResetStreamingToDefaults() {
    vr_graphics_quality = 3; // High (2496x2592)
    vr_frame_rate = 90;
    vr_bitrate_mbps = 150;
    sharpening_pct = 10;
    passthrough_environment = false;
    passthrough_hands = false;
    passthrough_desk_portal = false;
    configure_passthrough_requested = false;
    gamma = 1.00f;
    ssw_mode = 0; // Disabled
    snapdragon_sgsr = true;
    video_buffering = false;
    center_to_play_space = true;
    track_controllers = true;
    forward_tracking_to_pc = true;
    emulate_vive_trackers = false;
    emulate_index_controllers = false;
    streaming_color_vibrance = true;
    show_performance_overlay = true;
    settingsDirty = true;
}

// ============================================================================
// VxrVrMenuUi Implementation
// ============================================================================

VxrVrMenuUi::VxrVrMenuUi()
    : m_visible(true)
    , m_activeTab(VrMenuTab::COMPUTERS)
    , m_settingsDirty(false)
    , m_pointerX(0.5f)
    , m_pointerY(0.5f)
    , m_lastRenderWidth(1280)
    , m_lastRenderHeight(800) {
    Initialize();
}

VxrVrMenuUi::~VxrVrMenuUi() = default;

bool VxrVrMenuUi::Initialize() {
    m_settings.ResetToDefaults();
    m_settings.settingsDirty = false;
    m_settingsDirty = false;

    m_computers.clear();
#ifndef __ANDROID__
    m_computers.push_back({"DESKTOP-VXR-HOST", "192.168.1.61", true, true, 1200, 5});
    m_computers.push_back({"WORKSTATION-RTX", "192.168.1.104", false, true, 2400, 6});
#endif

    m_games.clear();
    m_games.push_back({"Half-Life: Alyx", "steam://rungameid/546560", true});
    m_games.push_back({"Beat Saber", "steam://rungameid/620980", true});
    m_games.push_back({"VRChat", "steam://rungameid/438100", true});
    m_games.push_back({"Assetto Corsa", "steam://rungameid/244210", true});
    m_games.push_back({"Blade & Sorcery", "steam://rungameid/629730", true});

    m_videos.clear();
    m_videos.push_back({"Cyberpunk_2077_Gameplay_4K.mp4", "14:22", VrVideoProjectionMode::FLAT_2D});
    m_videos.push_back({"Avatar_Way_Of_Water_3D_SBS.mkv", "03:12:10", VrVideoProjectionMode::SBS_3D});
    m_videos.push_back({"ISS_Spacewalk_VR180_8K.mp4", "28:45", VrVideoProjectionMode::VR_180});
    m_videos.push_back({"Dolomites_Aerial_Tour_VR360.mp4", "09:15", VrVideoProjectionMode::VR_360});

    return true;
}

void VxrVrMenuUi::SetActiveTab(VrMenuTab tab) {
    if (m_activeTab != tab) {
        m_activeTab = tab;
        m_hitBoxes.clear();
    }
}

uint32_t VxrVrMenuUi::GetVrRenderWidthForQuality(uint32_t quality_index) {
    switch (quality_index) {
        case 0: return 1440; // Potato
        case 1: return 1728; // Low
        case 2: return 2016; // Medium
        case 3: return 2496; // High (RTX 3070 / RX 6800 XT)
        case 4: return 2688; // Ultra
        case 5: return 3072; // Godlike
        default: return 2496;
    }
}

uint32_t VxrVrMenuUi::GetVrRenderHeightForQuality(uint32_t quality_index) {
    switch (quality_index) {
        case 0: return 1536; // Potato
        case 1: return 1824; // Low
        case 2: return 2112; // Medium
        case 3: return 2592; // High (RTX 3070 / RX 6800 XT)
        case 4: return 2784; // Ultra
        case 5: return 3216; // Godlike
        default: return 2592;
    }
}

const char* VxrVrMenuUi::GetVrQualityName(uint32_t quality_index) {
    switch (quality_index) {
        case 0: return "Potato";
        case 1: return "Low";
        case 2: return "Medium";
        case 3: return "High";
        case 4: return "Ultra";
        case 5: return "Godlike";
        default: return "High";
    }
}

uint32_t VxrVrMenuUi::GetVrRenderWidth() const {
    return GetVrRenderWidthForQuality(m_settings.vr_graphics_quality);
}

uint32_t VxrVrMenuUi::GetVrRenderHeight() const {
    return GetVrRenderHeightForQuality(m_settings.vr_graphics_quality);
}

void VxrVrMenuUi::SetComputers(const std::vector<VrComputerEntry>& computers) {
    m_computers = computers;
}

void VxrVrMenuUi::SetGames(const std::vector<VrGameEntry>& games) {
    m_games = games;
}

void VxrVrMenuUi::SetVideos(const std::vector<VrVideoEntry>& videos) {
    m_videos = videos;
}

// ============================================================================
// Software Rasterizer Primitives
// ============================================================================

void VxrVrMenuUi::FillRect(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, int h, uint32_t color) {
    if (!buf || w <= 0 || h <= 0) return;
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(static_cast<int>(bw), x + w);
    int y1 = std::min(static_cast<int>(bh), y + h);
    if (x0 >= x1 || y0 >= y1) return;

    for (int py = y0; py < y1; ++py) {
        uint32_t* row = buf + static_cast<size_t>(py) * bw;
        for (int px = x0; px < x1; ++px) {
            row[px] = color;
        }
    }
}

void VxrVrMenuUi::DrawRectBorder(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, int h, int thickness, uint32_t color) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    FillRect(buf, bw, bh, x, y, w, thickness, color);
    FillRect(buf, bw, bh, x, y + h - thickness, w, thickness, color);
    FillRect(buf, bw, bh, x, y, thickness, h, color);
    FillRect(buf, bw, bh, x + w - thickness, y, thickness, h, color);
}

void VxrVrMenuUi::DrawLine(uint32_t* buf, uint32_t bw, uint32_t bh, int x0, int y0, int x1, int y1, uint32_t color) {
    if (!buf) return;
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        if (x0 >= 0 && x0 < static_cast<int>(bw) && y0 >= 0 && y0 < static_cast<int>(bh)) {
            buf[static_cast<size_t>(y0) * bw + static_cast<size_t>(x0)] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void VxrVrMenuUi::DrawCircleFill(uint32_t* buf, uint32_t bw, uint32_t bh, int cx, int cy, int radius, uint32_t color) {
    if (!buf || radius <= 0) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        int py = cy + dy;
        if (py < 0 || py >= static_cast<int>(bh)) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                int px = cx + dx;
                if (px >= 0 && px < static_cast<int>(bw)) {
                    buf[static_cast<size_t>(py) * bw + static_cast<size_t>(px)] = color;
                }
            }
        }
    }
}

void VxrVrMenuUi::DrawCircleOutline(uint32_t* buf, uint32_t bw, uint32_t bh, int cx, int cy, int radius, uint32_t color) {
    if (!buf || radius <= 0) return;
    int rMin2 = (radius - 1) * (radius - 1);
    int rMax2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        int py = cy + dy;
        if (py < 0 || py >= static_cast<int>(bh)) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 >= rMin2 && d2 <= rMax2) {
                int px = cx + dx;
                if (px >= 0 && px < static_cast<int>(bw)) {
                    buf[static_cast<size_t>(py) * bw + static_cast<size_t>(px)] = color;
                }
            }
        }
    }
}

void VxrVrMenuUi::DrawChar5x7(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, char c, int scale, uint32_t color) {
    if (!buf || scale <= 0) return;
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = kFont5x7[static_cast<int>(c) - 32];
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if ((bits >> row) & 0x01u) {
                FillRect(buf, bw, bh, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void VxrVrMenuUi::DrawText5x7(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, const char* text, int scale, uint32_t color) {
    if (!buf || !text || scale <= 0) return;
    int cursorX = x;
    int advance = 6 * scale;
    for (const char* p = text; *p != '\0'; ++p) {
        DrawChar5x7(buf, bw, bh, cursorX, y, *p, scale, color);
        cursorX += advance;
    }
}

int VxrVrMenuUi::MeasureTextWidth5x7(const char* text, int scale) {
    if (!text || scale <= 0) return 0;
    size_t len = std::strlen(text);
    if (len == 0) return 0;
    return static_cast<int>(len * 6 * scale - scale);
}

// ============================================================================
// UI Widget Helpers
// ============================================================================

void VxrVrMenuUi::RegisterHitBox(uint32_t id, int x, int y, int w, int h, uint32_t bw, uint32_t bh, int32_t param) {
    if (bw == 0 || bh == 0) return;
    VrUiHitBox box;
    box.id = id;
    box.x0 = static_cast<float>(x) / static_cast<float>(bw);
    box.y0 = static_cast<float>(y) / static_cast<float>(bh);
    box.x1 = static_cast<float>(x + w) / static_cast<float>(bw);
    box.y1 = static_cast<float>(y + h) / static_cast<float>(bh);
    box.param = param;
    m_hitBoxes.push_back(box);
}

void VxrVrMenuUi::DrawCheckbox(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, const char* label, bool checked, uint32_t hitId) {
    const int boxSize = 16;
    FillRect(buf, bw, bh, x, y, boxSize, boxSize, checked ? COLOR_ACCENT_BLUE : COLOR_CONTROL_BG);
    DrawRectBorder(buf, bw, bh, x, y, boxSize, boxSize, 1, checked ? COLOR_ACCENT_BLUE : COLOR_BORDER);

    if (checked) {
        // Draw crisp checkmark inside box
        DrawLine(buf, bw, bh, x + 3, y + 8, x + 7, y + 12, COLOR_TEXT_PRIMARY);
        DrawLine(buf, bw, bh, x + 7, y + 12, x + 13, y + 4, COLOR_TEXT_PRIMARY);
        DrawLine(buf, bw, bh, x + 3, y + 9, x + 7, y + 13, COLOR_TEXT_PRIMARY);
        DrawLine(buf, bw, bh, x + 7, y + 13, x + 13, y + 5, COLOR_TEXT_PRIMARY);
    }

    DrawText5x7(buf, bw, bh, x + boxSize + 8, y + 4, label, 1, COLOR_TEXT_PRIMARY);
    int totalW = boxSize + 8 + MeasureTextWidth5x7(label, 1) + 8;
    RegisterHitBox(hitId, x, y, std::max(totalW, 180), boxSize + 4, bw, bh, 0);
}

void VxrVrMenuUi::DrawRadioButton(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, const char* label, bool selected, uint32_t hitId, int32_t param) {
    const int radius = 7;
    int cx = x + radius;
    int cy = y + radius;
    DrawCircleFill(buf, bw, bh, cx, cy, radius, COLOR_CONTROL_BG);
    DrawCircleOutline(buf, bw, bh, cx, cy, radius, selected ? COLOR_ACCENT_BLUE : COLOR_BORDER);
    if (selected) {
        DrawCircleFill(buf, bw, bh, cx, cy, 4, COLOR_ACCENT_BLUE);
    }
    DrawText5x7(buf, bw, bh, x + radius * 2 + 6, y + 3, label, 1, selected ? COLOR_TEXT_PRIMARY : COLOR_TEXT_SECONDARY);
    int totalW = radius * 2 + 6 + MeasureTextWidth5x7(label, 1) + 6;
    RegisterHitBox(hitId, x, y, totalW, radius * 2 + 4, bw, bh, param);
}

void VxrVrMenuUi::DrawSlider(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, const char* label, const char* valueText, float normVal, uint32_t hitId) {
    normVal = Clamp01(normVal);
    if (label && label[0] != '\0') {
        DrawText5x7(buf, bw, bh, x, y, label, 1, COLOR_TEXT_SECONDARY);
    }
    if (valueText && valueText[0] != '\0') {
        int valW = MeasureTextWidth5x7(valueText, 1);
        DrawText5x7(buf, bw, bh, x + w - valW, y, valueText, 1, COLOR_ACCENT_BLUE);
    }

    int trackY = y + 12;
    int trackH = 8;
    FillRect(buf, bw, bh, x, trackY, w, trackH, COLOR_CONTROL_BG);
    int fillW = static_cast<int>( std::round(normVal * static_cast<float>(w)) );
    if (fillW > 0) {
        FillRect(buf, bw, bh, x, trackY, fillW, trackH, COLOR_ACCENT_BLUE);
    }
    DrawRectBorder(buf, bw, bh, x, trackY, w, trackH, 1, COLOR_BORDER);

    // Thumb knob
    int knobX = x + fillW;
    DrawCircleFill(buf, bw, bh, knobX, trackY + trackH / 2, 6, COLOR_TEXT_PRIMARY);
    DrawCircleOutline(buf, bw, bh, knobX, trackY + trackH / 2, 6, COLOR_ACCENT_BLUE);

    RegisterHitBox(hitId, x, trackY - 4, w, trackH + 10, bw, bh, 0);
}

void VxrVrMenuUi::DrawButton(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, int h, const char* label, bool primaryBlue, uint32_t hitId, int32_t param) {
    uint32_t bg = primaryBlue ? COLOR_ACCENT_BLUE : COLOR_CONTROL_BG;
    FillRect(buf, bw, bh, x, y, w, h, bg);
    DrawRectBorder(buf, bw, bh, x, y, w, h, 1, primaryBlue ? COLOR_ACCENT_HOVER : COLOR_BORDER);

    int textW = MeasureTextWidth5x7(label, 1);
    int tx = x + (w - textW) / 2;
    int ty = y + (h - 7) / 2;
    DrawText5x7(buf, bw, bh, tx, ty, label, 1, COLOR_TEXT_PRIMARY);

    RegisterHitBox(hitId, x, y, w, h, bw, bh, param);
}

// ============================================================================
// Top Bar, Bottom Bar, and Left Sidebar Renderers
// ============================================================================

void VxrVrMenuUi::RenderTopBar(uint32_t* buf, uint32_t bw, uint32_t bh) {
    int barH = 36;
    FillRect(buf, bw, bh, 0, 0, static_cast<int>(bw), barH, COLOR_BG_TOPBAR);
    FillRect(buf, bw, bh, 0, barH - 1, static_cast<int>(bw), 1, COLOR_BORDER);

    // Hamburger Menu icon '≡' (3 horizontal bars) - click to toggle Full-Screen Desktop View
    int iconX = 16;
    int iconY = 11;
    FillRect(buf, bw, bh, iconX, iconY,      16, 2, COLOR_ACCENT_BLUE);
    FillRect(buf, bw, bh, iconX, iconY + 6,  16, 2, COLOR_ACCENT_BLUE);
    FillRect(buf, bw, bh, iconX, iconY + 12, 16, 2, COLOR_ACCENT_BLUE);
    RegisterHitBox(HIT_BTN_VIEW_DESKTOP, 6, 2, 36, 32, bw, bh, 0);

    // Clock & Battery
    DrawText5x7(buf, bw, bh, 46, 14, m_settings.clock_text.c_str(), 1, COLOR_TEXT_PRIMARY);

    char battBuf[32];
    std::snprintf(battBuf, sizeof(battBuf), "%u %%", m_settings.battery_pct);
    DrawText5x7(buf, bw, bh, 115, 14, battBuf, 1, COLOR_STATUS_GREEN);
    DrawRectBorder(buf, bw, bh, 146, 12, 18, 10, 1, COLOR_TEXT_SECONDARY);
    FillRect(buf, bw, bh, 148, 14, 12, 6, COLOR_STATUS_GREEN);

    // Right side Wi-Fi status bar: "5 GHz | 1,200 Mbps | 192.168.1.61"
    char wifiBuf[128];
    if (m_settings.wifi_link_mbps >= 1000) {
        std::snprintf(wifiBuf, sizeof(wifiBuf), "%u GHz | %u,%03u Mbps | %s",
                      m_settings.wifi_band_ghz,
                      m_settings.wifi_link_mbps / 1000,
                      m_settings.wifi_link_mbps % 1000,
                      m_settings.local_ip.c_str());
    } else {
        std::snprintf(wifiBuf, sizeof(wifiBuf), "%u GHz | %u Mbps | %s",
                      m_settings.wifi_band_ghz,
                      m_settings.wifi_link_mbps,
                      m_settings.local_ip.c_str());
    }
    int wifiW = MeasureTextWidth5x7(wifiBuf, 1);
    DrawText5x7(buf, bw, bh, static_cast<int>(bw) - wifiW - 20, 14, wifiBuf, 1, COLOR_TEXT_SECONDARY);
}

void VxrVrMenuUi::RenderBottomBar(uint32_t* buf, uint32_t bw, uint32_t bh) {
    int barH = 34;
    int barY = static_cast<int>(bh) - barH;
    FillRect(buf, bw, bh, 0, barY, static_cast<int>(bw), barH, COLOR_BG_BOTTOMBAR);
    FillRect(buf, bw, bh, 0, barY, static_cast<int>(bw), 1, COLOR_BORDER);

    char leftInfo[128];
    std::snprintf(leftInfo, sizeof(leftInfo), "Headset: %s    |    Ask for help", m_settings.headset_model.c_str());
    DrawText5x7(buf, bw, bh, 18, barY + 13, leftInfo, 1, COLOR_TEXT_SECONDARY);

    uint32_t activeFps = (m_activeTab == VrMenuTab::STREAMING) ? m_settings.vr_frame_rate : m_settings.desktop_frame_rate;
    uint32_t activeBitrate = (m_activeTab == VrMenuTab::STREAMING) ? m_settings.vr_bitrate_mbps : m_settings.desktop_bitrate_mbps;

    char rightInfo[160];
    std::snprintf(rightInfo, sizeof(rightInfo),
                  "Frame Rate: %u fps   |   Video Frame Rate: %u fps   |   Video Bitrate: %u Mbps",
                  activeFps, activeFps, activeBitrate);
    int rw = MeasureTextWidth5x7(rightInfo, 1);
    DrawText5x7(buf, bw, bh, static_cast<int>(bw) - rw - 18, barY + 13, rightInfo, 1, COLOR_TEXT_SECONDARY);
}

void VxrVrMenuUi::RenderSidebar(uint32_t* buf, uint32_t bw, uint32_t bh) {
    int topH = 36;
    int botH = 34;
    int sideW = static_cast<int>(static_cast<float>(bw) * 0.18f);
    int sideH = static_cast<int>(bh) - topH - botH;

    FillRect(buf, bw, bh, 0, topH, sideW, sideH, COLOR_BG_SIDEBAR);
    FillRect(buf, bw, bh, sideW - 1, topH, 1, sideH, COLOR_BORDER);

    struct TabItem {
        VrMenuTab tab;
        const char* label;
        uint32_t hitId;
    };
    static const TabItem kTabs[7] = {
        { VrMenuTab::COMPUTERS,    "COMPUTERS",    HIT_TAB_COMPUTERS },
        { VrMenuTab::ENVIRONMENTS, "ENVIRONMENTS", HIT_TAB_ENVIRONMENTS },
        { VrMenuTab::GAMES,        "GAMES",        HIT_TAB_GAMES },
        { VrMenuTab::INPUT,        "INPUT",        HIT_TAB_INPUT },
        { VrMenuTab::SETTINGS,     "SETTINGS",     HIT_TAB_SETTINGS },
        { VrMenuTab::STREAMING,    "STREAMING",    HIT_TAB_STREAMING },
        { VrMenuTab::VIDEOS,       "VIDEOS",       HIT_TAB_VIDEOS }
    };

    int itemH = 44;
    int startY = topH + 18;
    for (int i = 0; i < 7; ++i) {
        int iy = startY + i * itemH;
        bool active = (m_activeTab == kTabs[i].tab);
        if (active) {
            FillRect(buf, bw, bh, 0, iy, sideW - 1, itemH - 2, COLOR_BG_CARD);
            FillRect(buf, bw, bh, 0, iy, 4, itemH - 2, COLOR_ACCENT_BLUE);
        }
        DrawText5x7(buf, bw, bh, 22, iy + 16, kTabs[i].label, 1, active ? COLOR_ACCENT_BLUE : COLOR_TEXT_SECONDARY);
        RegisterHitBox(kTabs[i].hitId, 0, iy, sideW, itemH, bw, bh, static_cast<int32_t>(kTabs[i].tab));
    }

    // Bottom sidebar buttons: Launch SteamVR & Quit
    int quitY = static_cast<int>(bh) - botH - 48;
    int steamY = quitY - 46;
    DrawButton(buf, bw, bh, 14, steamY, sideW - 28, 36, "Launch SteamVR", true, HIT_BTN_STEAMVR);
    DrawButton(buf, bw, bh, 14, quitY,  sideW - 28, 36, "Quit", false, HIT_BTN_QUIT);
}

// ============================================================================
// Tab 1: COMPUTERS (Screenshot 1)
// ============================================================================

void VxrVrMenuUi::RenderComputersTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    DrawText5x7(buf, bw, bh, contentX + 24, contentY + 20, "COMPUTERS", 2, COLOR_TEXT_PRIMARY);
    DrawText5x7(buf, bw, bh, contentX + 24, contentY + 44, "Select a computer on your local network to connect", 1, COLOR_TEXT_SECONDARY);

    int cardY = contentY + 72;
    if (m_computers.empty()) {
        int emptyX = contentX + 24;
        int emptyW = contentW - 48;
        FillRect(buf, bw, bh, emptyX, cardY, emptyW, 88, COLOR_BG_CARD);
        DrawRectBorder(buf, bw, bh, emptyX, cardY, emptyW, 88, 1, COLOR_BORDER);
        DrawText5x7(buf, bw, bh, emptyX + 24, cardY + 26, "Searching for Project VXR Streamer on local network...", 1, COLOR_ACCENT_BLUE);
        DrawText5x7(buf, bw, bh, emptyX + 24, cardY + 48, "Ensure ProjectVXR.exe is running on your PC (UDP :38810 / mDNS :5353)", 1, COLOR_TEXT_SECONDARY);
    }
    for (size_t i = 0; i < m_computers.size(); ++i) {
        const VrComputerEntry& pc = m_computers[i];
        int cardX = contentX + 24;
        int cardW = contentW - 48;
        int cardH = 88;

        FillRect(buf, bw, bh, cardX, cardY, cardW, cardH, COLOR_BG_CARD);
        DrawRectBorder(buf, bw, bh, cardX, cardY, cardW, cardH, 1, pc.connected ? COLOR_ACCENT_BLUE : COLOR_BORDER);

        // Windows Monitor Icon
        int monX = cardX + 20;
        int monY = cardY + 20;
        DrawRectBorder(buf, bw, bh, monX, monY, 44, 30, 2, COLOR_ACCENT_BLUE);
        FillRect(buf, bw, bh, monX + 4, monY + 4, 36, 22, COLOR_BG_PANEL);
        FillRect(buf, bw, bh, monX + 18, monY + 30, 8, 8, COLOR_ACCENT_BLUE);
        FillRect(buf, bw, bh, monX + 10, monY + 38, 24, 3, COLOR_ACCENT_BLUE);

        // Hostname & IP
        DrawText5x7(buf, bw, bh, monX + 64, cardY + 22, pc.hostname.c_str(), 2, COLOR_TEXT_PRIMARY);

        char subInfo[128];
        std::snprintf(subInfo, sizeof(subInfo), "%s   |   %u GHz (%u Mbps)",
                      pc.ip_address.c_str(), pc.wifi_band_ghz, pc.link_speed_mbps);
        DrawText5x7(buf, bw, bh, monX + 64, cardY + 46, subInfo, 1, COLOR_TEXT_SECONDARY);

        // Status indicator
        const char* statusStr = pc.connected ? "Connected - Live Desktop Ready" : (pc.available ? "Available" : "Offline");
        uint32_t statusColor = pc.connected ? COLOR_STATUS_GREEN : (pc.available ? COLOR_TEXT_SECONDARY : COLOR_DANGER_RED);
        DrawCircleFill(buf, bw, bh, monX + 68, cardY + 67, 4, statusColor);
        DrawText5x7(buf, bw, bh, monX + 78, cardY + 64, statusStr, 1, statusColor);

        if (pc.connected) {
            DrawButton(buf, bw, bh, cardX + cardW - 268, cardY + 26, 118, 36, "View Desktop", true,
                       HIT_BTN_VIEW_DESKTOP, static_cast<int32_t>(i));
        }

        // Connect / Disconnect button
        const char* btnLabel = pc.connected ? "Disconnect" : "Connect";
        DrawButton(buf, bw, bh, cardX + cardW - 140, cardY + 26, 116, 36, btnLabel, !pc.connected,
                   HIT_COMPUTER_TOGGLE_CONNECT, static_cast<int32_t>(i));

        cardY += cardH + 16;
    }
    (void)contentH;
}

// ============================================================================
// Tab 2: ENVIRONMENTS
// ============================================================================

void VxrVrMenuUi::RenderEnvironmentsTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    DrawText5x7(buf, bw, bh, contentX + 24, contentY + 20, "ENVIRONMENTS", 2, COLOR_TEXT_PRIMARY);

    static const char* kEnvNames[5] = {
        "Home Theater",
        "Modern Apartment",
        "Cinema Auditorium",
        "Gaming Room",
        "Passthrough Void"
    };

    int listX = contentX + 24;
    int listY = contentY + 56;
    for (uint32_t i = 0; i < 5; ++i) {
        int itemY = listY + static_cast<int>(i) * 44;
        bool selected = (m_settings.selected_environment == i);
        FillRect(buf, bw, bh, listX, itemY, 320, 36, selected ? COLOR_BG_CARD : COLOR_BG_PANEL);
        DrawRectBorder(buf, bw, bh, listX, itemY, 320, 36, 1, selected ? COLOR_ACCENT_BLUE : COLOR_BORDER);
        DrawRadioButton(buf, bw, bh, listX + 12, itemY + 10, kEnvNames[i], selected, HIT_ENV_SELECT, static_cast<int32_t>(i));
    }

    int sliderX = contentX + 380;
    int sliderW = contentW - 420;
    char valBuf[32];

    std::snprintf(valBuf, sizeof(valBuf), "%.2f m", m_settings.screen_distance_m);
    float distNorm = (m_settings.screen_distance_m - 0.8f) / (6.0f - 0.8f);
    DrawSlider(buf, bw, bh, sliderX, contentY + 66, sliderW, "Screen Distance", valBuf, distNorm, HIT_ENV_SLIDER_DISTANCE);

    std::snprintf(valBuf, sizeof(valBuf), "%.2fx", m_settings.screen_size_scale);
    float sizeNorm = (m_settings.screen_size_scale - 0.5f) / (3.0f - 0.5f);
    DrawSlider(buf, bw, bh, sliderX, contentY + 126, sliderW, "Screen Size", valBuf, sizeNorm, HIT_ENV_SLIDER_SIZE);

    std::snprintf(valBuf, sizeof(valBuf), "%d %%", static_cast<int>(std::round(m_settings.screen_curvature * 100.0f)));
    DrawSlider(buf, bw, bh, sliderX, contentY + 186, sliderW, "Screen Curvature", valBuf, m_settings.screen_curvature, HIT_ENV_SLIDER_CURVE);
    (void)contentH;
}

// ============================================================================
// Tab 3: GAMES
// ============================================================================

void VxrVrMenuUi::RenderGamesTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    DrawText5x7(buf, bw, bh, contentX + 24, contentY + 20, "PC VR GAMES", 2, COLOR_TEXT_PRIMARY);
    DrawText5x7(buf, bw, bh, contentX + 24, contentY + 44, "Click Launch to start OpenXR / SteamVR streaming immediately", 1, COLOR_TEXT_SECONDARY);

    int rowY = contentY + 72;
    for (size_t i = 0; i < m_games.size(); ++i) {
        const VrGameEntry& game = m_games[i];
        int rowX = contentX + 24;
        int rowW = contentW - 48;
        int rowH = 54;

        FillRect(buf, bw, bh, rowX, rowY, rowW, rowH, COLOR_BG_CARD);
        DrawRectBorder(buf, bw, bh, rowX, rowY, rowW, rowH, 1, COLOR_BORDER);

        DrawText5x7(buf, bw, bh, rowX + 18, rowY + 14, game.title.c_str(), 2, COLOR_TEXT_PRIMARY);
        DrawText5x7(buf, bw, bh, rowX + 18, rowY + 34, game.app_id.c_str(), 1, COLOR_TEXT_MUTED);

        DrawButton(buf, bw, bh, rowX + rowW - 120, rowY + 11, 100, 32, "Launch", true,
                   HIT_GAME_LAUNCH, static_cast<int32_t>(i));
        rowY += rowH + 10;
    }
    (void)contentH;
}

// ============================================================================
// Tab 4: INPUT (Screenshot 2)
// ============================================================================

void VxrVrMenuUi::RenderInputTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    int col1X = contentX + 20;
    int y = contentY + 16;

    // Section 1: Input Options (9 checkboxes)
    DrawText5x7(buf, bw, bh, col1X, y, "Input Options", 2, COLOR_ACCENT_BLUE);
    y += 22;
    const int stepY = 24;
    DrawCheckbox(buf, bw, bh, col1X, y, "Controllers interact with desktop", m_settings.controllers_interact_desktop, HIT_IN_CONTROLLERS_INTERACT); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Hands interact with desktop", m_settings.hands_interact_desktop, HIT_IN_HANDS_INTERACT); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Pointer stabilization", m_settings.pointer_stabilization, HIT_IN_POINTER_STABILIZATION); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Automatically hide controllers", m_settings.auto_hide_controllers, HIT_IN_AUTO_HIDE_CONTROLLERS); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Thumbstick vertical scroll", m_settings.thumbstick_vertical_scroll, HIT_IN_THUMB_VERT_SCROLL); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Thumbstick horizontal scroll", m_settings.thumbstick_horizontal_scroll, HIT_IN_THUMB_HORIZ_SCROLL); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Hand tracking", m_settings.hand_tracking, HIT_IN_HAND_TRACKING); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Press grip to grab/resize screen", m_settings.press_grip_to_grab_resize, HIT_IN_GRIP_GRAB_RESIZE); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Hold menu button to switch out of VR", m_settings.hold_menu_to_switch_out_vr, HIT_IN_HOLD_MENU_SWITCH); y += stepY + 12;

    // Section 2: Gamepad (2 checkboxes)
    DrawText5x7(buf, bw, bh, col1X, y, "Gamepad", 2, COLOR_ACCENT_BLUE);
    y += 22;
    DrawCheckbox(buf, bw, bh, col1X, y, "Emulate gamepad on PC", m_settings.emulate_gamepad_on_pc, HIT_IN_EMULATE_GAMEPAD); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "Use Touch controllers as gamepad", m_settings.use_touch_controllers_as_gamepad, HIT_IN_TOUCH_AS_GAMEPAD); y += stepY + 12;

    // Section 3: Emulate D-pad and Start button (3 checkboxes)
    DrawText5x7(buf, bw, bh, col1X, y, "Emulate D-pad and Start button", 2, COLOR_ACCENT_BLUE);
    y += 22;
    DrawCheckbox(buf, bw, bh, col1X, y, "When right grip is pressed", m_settings.dpad_when_right_grip_pressed, HIT_IN_DPAD_RIGHT_GRIP); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "When right thumbstick is pressed", m_settings.dpad_when_right_thumbstick_pressed, HIT_IN_DPAD_RIGHT_THUMB); y += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y, "When right thumbrest is active", m_settings.dpad_when_right_thumbrest_active, HIT_IN_DPAD_RIGHT_THUMBREST);

    // Right Column: Quest Touch Controller Visual Diagram + Callout lines + Create Desk Portal button
    int diagX = contentX + 365;
    int diagY = contentY + 20;
    int diagW = contentW - 385;
    int diagH = contentH - 40;

    FillRect(buf, bw, bh, diagX, diagY, diagW, diagH, COLOR_BG_PANEL);
    DrawRectBorder(buf, bw, bh, diagX, diagY, diagW, diagH, 1, COLOR_BORDER);

    DrawText5x7(buf, bw, bh, diagX + 16, diagY + 14, "Quest Touch Controller Mapping", 2, COLOR_TEXT_PRIMARY);

    // Left & Right Controller silhouettes
    int leftCx  = diagX + diagW / 2 - 80;
    int rightCx = diagX + diagW / 2 + 80;
    int ringCy  = diagY + 190;

    // Left Controller silhouette (top face + grip handle)
    DrawCircleFill(buf, bw, bh, leftCx, ringCy, 36, COLOR_BG_CARD);
    DrawCircleOutline(buf, bw, bh, leftCx, ringCy, 36, COLOR_ACCENT_BLUE);
    FillRect(buf, bw, bh, leftCx - 14, ringCy + 28, 28, 68, COLOR_BG_CARD);
    DrawRectBorder(buf, bw, bh, leftCx - 14, ringCy + 28, 28, 68, 1, COLOR_ACCENT_BLUE);
    // Left buttons: Thumbstick, X, Y, Menu
    DrawCircleFill(buf, bw, bh, leftCx - 10, ringCy - 8, 7, COLOR_ACCENT_BLUE); // Thumbstick
    DrawCircleFill(buf, bw, bh, leftCx + 10, ringCy - 4, 4, COLOR_TEXT_PRIMARY); // Y
    DrawCircleFill(buf, bw, bh, leftCx + 6,  ringCy + 10, 4, COLOR_TEXT_PRIMARY); // X
    DrawCircleFill(buf, bw, bh, leftCx - 8,  ringCy + 14, 3, COLOR_TEXT_SECONDARY); // Menu

    // Right Controller silhouette (top face + grip handle)
    DrawCircleFill(buf, bw, bh, rightCx, ringCy, 36, COLOR_BG_CARD);
    DrawCircleOutline(buf, bw, bh, rightCx, ringCy, 36, COLOR_ACCENT_BLUE);
    FillRect(buf, bw, bh, rightCx - 14, ringCy + 28, 28, 68, COLOR_BG_CARD);
    DrawRectBorder(buf, bw, bh, rightCx - 14, ringCy + 28, 28, 68, 1, COLOR_ACCENT_BLUE);
    // Right buttons: Thumbstick, A, B, Oculus
    DrawCircleFill(buf, bw, bh, rightCx + 10, ringCy - 8, 7, COLOR_ACCENT_BLUE); // Thumbstick
    DrawCircleFill(buf, bw, bh, rightCx - 10, ringCy - 4, 4, COLOR_TEXT_PRIMARY); // B
    DrawCircleFill(buf, bw, bh, rightCx - 6,  ringCy + 10, 4, COLOR_TEXT_PRIMARY); // A
    DrawCircleFill(buf, bw, bh, rightCx + 8,  ringCy + 14, 3, COLOR_TEXT_SECONDARY); // Oculus

    // Callout lines & labels matching Screenshot 2
    DrawLine(buf, bw, bh, leftCx + 10, ringCy - 4, diagX + 20, diagY + 60, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + 20, diagY + 48, "Y: Switch Monitor", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, leftCx + 6, ringCy + 10, diagX + 20, diagY + 92, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + 20, diagY + 80, "X: Toggle keyboard", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, leftCx - 8, ringCy + 14, diagX + 20, diagY + 124, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + 20, diagY + 112, "Menu: Toggle menu", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, rightCx + 10, ringCy - 8, diagX + diagW - 220, diagY + 60, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + diagW - 310, diagY + 48, "Thumbstick: Move: Mouse scroll / Press: Middle click", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, rightCx - 10, ringCy - 4, diagX + diagW - 160, diagY + 92, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + diagW - 160, diagY + 80, "B: Right click", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, rightCx - 6, ringCy + 10, diagX + diagW - 160, diagY + 124, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + diagW - 160, diagY + 112, "A: Left click", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, rightCx + 8, ringCy + 14, diagX + diagW - 180, diagY + 310, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + diagW - 190, diagY + 314, "Oculus: Hold: Reset view", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, leftCx, ringCy - 36, diagX + 20, diagY + 310, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + 20, diagY + 314, "Trigger: Left click", 1, COLOR_TEXT_PRIMARY);

    DrawLine(buf, bw, bh, leftCx + 14, ringCy + 60, diagX + 20, diagY + 340, COLOR_ACCENT_BLUE);
    DrawText5x7(buf, bw, bh, diagX + 20, diagY + 344, "Grip: Grab/resize screen", 1, COLOR_TEXT_PRIMARY);

    // Create Desk Portal button at bottom right of diagram
    DrawButton(buf, bw, bh, diagX + diagW - 190, diagY + diagH - 48, 170, 34,
               "Create Desk Portal", true, HIT_IN_CREATE_DESK_PORTAL);
}

// ============================================================================
// Tab 5: SETTINGS (Screenshot 3)
// ============================================================================

void VxrVrMenuUi::RenderSettingsTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    int col1X = contentX + 20;
    int col2X = contentX + contentW / 2 + 10;
    int colW  = contentW / 2 - 36;
    int y1 = contentY + 14;
    const int stepY = 23;

    // --- Left Column ---
    // Computers
    DrawText5x7(buf, bw, bh, col1X, y1, "Computers", 2, COLOR_ACCENT_BLUE); y1 += 20;
    DrawCheckbox(buf, bw, bh, col1X, y1, "Auto-connect", m_settings.auto_connect, HIT_SET_AUTO_CONNECT); y1 += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y1, "Show games on connect", m_settings.show_games_on_connect, HIT_SET_SHOW_GAMES); y1 += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y1, "Use optimal resolution", m_settings.use_optimal_resolution, HIT_SET_OPTIMAL_RES); y1 += stepY;
    DrawCheckbox(buf, bw, bh, col1X, y1, "Arrange monitors on recenter", m_settings.arrange_monitors_on_recenter, HIT_SET_ARRANGE_ON_RECENTER); y1 += stepY;
    DrawButton(buf, bw, bh, col1X, y1, 170, 26, "Arrange monitors now", false, HIT_SET_ARRANGE_NOW); y1 += 36;

    // Environment Quality: Low, Medium, High
    DrawText5x7(buf, bw, bh, col1X, y1, "Environment Quality", 2, COLOR_ACCENT_BLUE); y1 += 20;
    DrawRadioButton(buf, bw, bh, col1X,       y1, "Low",    m_settings.environment_quality == 0, HIT_SET_ENV_QUALITY, 0);
    DrawRadioButton(buf, bw, bh, col1X + 80,  y1, "Medium", m_settings.environment_quality == 1, HIT_SET_ENV_QUALITY, 1);
    DrawRadioButton(buf, bw, bh, col1X + 175, y1, "High",   m_settings.environment_quality == 2, HIT_SET_ENV_QUALITY, 2);
    y1 += 26;

    // Frame Rate: 60, 72, 80, 90, 96, 100, 120 fps
    DrawText5x7(buf, bw, bh, col1X, y1, "Frame Rate", 2, COLOR_ACCENT_BLUE); y1 += 20;
    for (int i = 0; i < 7; ++i) {
        char fpsLbl[16];
        std::snprintf(fpsLbl, sizeof(fpsLbl), "%u", kSupportedFps[i]);
        int rx = col1X + (i % 4) * 72;
        int ry = y1 + (i / 4) * 20;
        DrawRadioButton(buf, bw, bh, rx, ry, fpsLbl, m_settings.desktop_frame_rate == kSupportedFps[i],
                        HIT_SET_FRAME_RATE, static_cast<int32_t>(kSupportedFps[i]));
    }
    y1 += 46;

    // Desktop Bitrate: 10..200 Mbps (default 120 Mbps)
    char brText[32];
    std::snprintf(brText, sizeof(brText), "%u Mbps", m_settings.desktop_bitrate_mbps);
    float brNorm = static_cast<float>(m_settings.desktop_bitrate_mbps - 10) / 190.0f;
    DrawSlider(buf, bw, bh, col1X, y1, colW, "Desktop Bitrate", brText, brNorm, HIT_SET_DESKTOP_BITRATE);
    y1 += 34;

    // Video & Screen Transparency
    DrawText5x7(buf, bw, bh, col1X, y1 + 6, "Video & Screen Transparency", 1, COLOR_TEXT_SECONDARY);
    DrawButton(buf, bw, bh, col1X + 190, y1, 100, 24, "Configure...", false, HIT_SET_CONFIG_TRANSPARENCY);
    y1 += 32;

    // Screen Brightness: 10..100 %
    char brightText[32];
    std::snprintf(brightText, sizeof(brightText), "%u %%", m_settings.screen_brightness_pct);
    float brightNorm = static_cast<float>(m_settings.screen_brightness_pct - 10) / 90.0f;
    DrawSlider(buf, bw, bh, col1X, y1, colW, "Screen Brightness", brightText, brightNorm, HIT_SET_BRIGHTNESS);
    y1 += 34;

    // Dynamic Lighting
    DrawText5x7(buf, bw, bh, col1X, y1, "Dynamic Lighting", 2, COLOR_ACCENT_BLUE); y1 += 18;
    DrawRadioButton(buf, bw, bh, col1X, y1, "Disabled", m_settings.dynamic_lighting == 0, HIT_SET_DYNAMIC_LIGHTING, 0); y1 += 18;
    DrawRadioButton(buf, bw, bh, col1X, y1, "Enabled when controller inactive", m_settings.dynamic_lighting == 1, HIT_SET_DYNAMIC_LIGHTING, 1); y1 += 18;
    DrawRadioButton(buf, bw, bh, col1X, y1, "Always enabled", m_settings.dynamic_lighting == 2, HIT_SET_DYNAMIC_LIGHTING, 2);

    // --- Right Column ---
    int y2 = contentY + 14;
    // Audio
    DrawText5x7(buf, bw, bh, col2X, y2, "Audio", 2, COLOR_ACCENT_BLUE); y2 += 20;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Background music when disconnected", m_settings.bg_music_when_disconnected, HIT_SET_BG_MUSIC); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Microphone passthrough", m_settings.microphone_passthrough, HIT_SET_MIC_PASSTHROUGH); y2 += stepY + 4;

    char micVolText[32];
    std::snprintf(micVolText, sizeof(micVolText), "%u %%", m_settings.mic_volume_pct);
    float micNorm = static_cast<float>(m_settings.mic_volume_pct) / 100.0f;
    DrawSlider(buf, bw, bh, col2X, y2, colW, "Microphone Volume", micVolText, micNorm, HIT_SET_MIC_VOLUME);
    y2 += 34;

    DrawCheckbox(buf, bw, bh, col2X, y2, "Noise cancellation", m_settings.noise_cancellation, HIT_SET_NOISE_CANCEL); y2 += stepY + 16;

    // Advanced Options
    DrawText5x7(buf, bw, bh, col2X, y2, "Advanced Options", 2, COLOR_ACCENT_BLUE); y2 += 22;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Allow custom orientation", m_settings.allow_custom_orientation, HIT_SET_CUSTOM_ORIENTATION); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Boost clock rates", m_settings.boost_clock_rates, HIT_SET_BOOST_CLOCKS); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Copy screenshots to desktop", m_settings.copy_screenshots_to_desktop, HIT_SET_COPY_SCREENSHOTS); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Increase color vibrance", m_settings.increase_color_vibrance, HIT_SET_COLOR_VIBRANCE); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Remove head lock delay", m_settings.remove_head_lock_delay, HIT_SET_REMOVE_HEAD_DELAY);
    (void)contentH;
}

// ============================================================================
// Tab 6: STREAMING (Screenshot 4)
// ============================================================================

void VxrVrMenuUi::RenderStreamingTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    int col1X = contentX + 20;
    int col2X = contentX + contentW / 2 + 10;
    int colW  = contentW / 2 - 36;
    int y1 = contentY + 14;
    const int stepY = 23;

    // --- Left Column ---
    // VR Graphics Quality (0=Potato .. 5=Godlike)
    DrawText5x7(buf, bw, bh, col1X, y1, "VR Graphics Quality", 2, COLOR_ACCENT_BLUE); y1 += 20;
    static const char* kQualityLabels[6] = {
        "Potato (1440x1536)",
        "Low (1728x1824)",
        "Medium (2016x2112)",
        "High (2496x2592, RTX 3070 / RX 6800 XT)",
        "Ultra (2688x2784)",
        "Godlike (3072x3216)"
    };
    for (int i = 0; i < 6; ++i) {
        DrawRadioButton(buf, bw, bh, col1X, y1, kQualityLabels[i],
                        m_settings.vr_graphics_quality == static_cast<uint32_t>(i),
                        HIT_STR_VR_QUALITY, i);
        y1 += 19;
    }
    y1 += 6;

    // VR Frame Rate
    DrawText5x7(buf, bw, bh, col1X, y1, "VR Frame Rate", 2, COLOR_ACCENT_BLUE); y1 += 20;
    for (int i = 0; i < 7; ++i) {
        char fpsLbl[16];
        std::snprintf(fpsLbl, sizeof(fpsLbl), "%u", kSupportedFps[i]);
        int rx = col1X + (i % 4) * 72;
        int ry = y1 + (i / 4) * 20;
        DrawRadioButton(buf, bw, bh, rx, ry, fpsLbl, m_settings.vr_frame_rate == kSupportedFps[i],
                        HIT_STR_VR_FPS, static_cast<int32_t>(kSupportedFps[i]));
    }
    y1 += 44;

    // VR Bitrate: 30..500 Mbps (default 150 Mbps)
    char vrBrText[32];
    std::snprintf(vrBrText, sizeof(vrBrText), "%u Mbps", m_settings.vr_bitrate_mbps);
    float vrBrNorm = static_cast<float>(m_settings.vr_bitrate_mbps - 30) / 470.0f;
    DrawSlider(buf, bw, bh, col1X, y1, colW, "VR Bitrate", vrBrText, vrBrNorm, HIT_STR_VR_BITRATE);
    y1 += 32;

    // Sharpening: 0..100 % (default 10 %)
    char sharpText[32];
    std::snprintf(sharpText, sizeof(sharpText), "%u %%", m_settings.sharpening_pct);
    float sharpNorm = static_cast<float>(m_settings.sharpening_pct) / 100.0f;
    DrawSlider(buf, bw, bh, col1X, y1, colW, "Sharpening", sharpText, sharpNorm, HIT_STR_SHARPENING);
    y1 += 32;

    // VR Passthrough
    DrawText5x7(buf, bw, bh, col1X, y1, "VR Passthrough", 2, COLOR_ACCENT_BLUE);
    DrawButton(buf, bw, bh, col1X + 180, y1 - 4, 100, 22, "Configure...", false, HIT_STR_CONFIG_PASSTHROUGH);
    y1 += 20;
    DrawCheckbox(buf, bw, bh, col1X,       y1, "Environment", m_settings.passthrough_environment, HIT_STR_PASS_ENV);
    DrawCheckbox(buf, bw, bh, col1X + 115, y1, "Hands",       m_settings.passthrough_hands, HIT_STR_PASS_HANDS);
    DrawCheckbox(buf, bw, bh, col1X + 195, y1, "Desk portal", m_settings.passthrough_desk_portal, HIT_STR_PASS_PORTAL);
    y1 += 24;

    // Gamma: 0.50..1.50 (default 1.00)
    char gammaText[32];
    std::snprintf(gammaText, sizeof(gammaText), "%.2f", m_settings.gamma);
    float gammaNorm = (m_settings.gamma - 0.50f) / 1.00f;
    DrawSlider(buf, bw, bh, col1X, y1, colW, "Gamma", gammaText, gammaNorm, HIT_STR_GAMMA);

    // --- Right Column ---
    int y2 = contentY + 14;
    // Synchronous Spacewarp (SSW): 0=Disabled, 1=Always enabled, 2=Automatic
    DrawText5x7(buf, bw, bh, col2X, y2, "Synchronous Spacewarp (SSW)", 2, COLOR_ACCENT_BLUE); y2 += 20;
    DrawRadioButton(buf, bw, bh, col2X,       y2, "Disabled",       m_settings.ssw_mode == 0, HIT_STR_SSW_MODE, 0);
    DrawRadioButton(buf, bw, bh, col2X + 95,  y2, "Always enabled", m_settings.ssw_mode == 1, HIT_STR_SSW_MODE, 1);
    DrawRadioButton(buf, bw, bh, col2X + 225, y2, "Automatic",      m_settings.ssw_mode == 2, HIT_STR_SSW_MODE, 2);
    y2 += 28;

    // Advanced Options (9 checkboxes) + Reset to Defaults button
    DrawText5x7(buf, bw, bh, col2X, y2, "Advanced Options", 2, COLOR_ACCENT_BLUE); y2 += 22;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Snapdragon Game Super Resolution (SGSR)", m_settings.snapdragon_sgsr, HIT_STR_SGSR); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Video buffering", m_settings.video_buffering, HIT_STR_VIDEO_BUFFERING); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Center to play space", m_settings.center_to_play_space, HIT_STR_CENTER_PLAY_SPACE); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Track controllers", m_settings.track_controllers, HIT_STR_TRACK_CONTROLLERS); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Forward tracking data to PC", m_settings.forward_tracking_to_pc, HIT_STR_FORWARD_TRACKING); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Emulate Vive trackers", m_settings.emulate_vive_trackers, HIT_STR_EMULATE_VIVE); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Emulate Index controllers", m_settings.emulate_index_controllers, HIT_STR_EMULATE_INDEX); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Increase color vibrance", m_settings.streaming_color_vibrance, HIT_STR_COLOR_VIBRANCE); y2 += stepY;
    DrawCheckbox(buf, bw, bh, col2X, y2, "Show performance overlay", m_settings.show_performance_overlay, HIT_STR_PERF_OVERLAY); y2 += stepY + 12;

    DrawButton(buf, bw, bh, col2X, y2, 160, 32, "Reset to Defaults", false, HIT_STR_RESET_DEFAULTS);
    (void)contentH;
}

// ============================================================================
// Tab 7: VIDEOS
// ============================================================================

void VxrVrMenuUi::RenderVideosTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH) {
    DrawText5x7(buf, bw, bh, contentX + 24, contentY + 20, "VIDEOS (PC MEDIA)", 2, COLOR_TEXT_PRIMARY);

    // Projection Mode Selector: 2D / 3D SBS / VR180 / VR360
    int modeY = contentY + 52;
    DrawText5x7(buf, bw, bh, contentX + 24, modeY + 4, "Projection Mode:", 1, COLOR_TEXT_SECONDARY);
    static const char* kProjLabels[4] = { "2D", "3D SBS", "VR180", "VR360" };
    for (int i = 0; i < 4; ++i) {
        bool active = (static_cast<int>(m_settings.active_video_projection) == i);
        DrawButton(buf, bw, bh, contentX + 140 + i * 92, modeY - 4, 82, 26, kProjLabels[i], active,
                   HIT_VID_SELECT_PROJECTION, i);
    }

    int rowY = contentY + 94;
    for (size_t i = 0; i < m_videos.size(); ++i) {
        const VrVideoEntry& vid = m_videos[i];
        int rowX = contentX + 24;
        int rowW = contentW - 48;
        int rowH = 48;
        bool selected = (m_settings.selected_video_index == static_cast<int32_t>(i));

        FillRect(buf, bw, bh, rowX, rowY, rowW, rowH, COLOR_BG_CARD);
        DrawRectBorder(buf, bw, bh, rowX, rowY, rowW, rowH, 1, selected ? COLOR_ACCENT_BLUE : COLOR_BORDER);

        DrawText5x7(buf, bw, bh, rowX + 16, rowY + 14, vid.filename.c_str(), 1, COLOR_TEXT_PRIMARY);
        DrawText5x7(buf, bw, bh, rowX + 16, rowY + 28, vid.duration_str.c_str(), 1, COLOR_TEXT_SECONDARY);

        DrawButton(buf, bw, bh, rowX + rowW - 100, rowY + 10, 84, 28, "Play", selected,
                   HIT_VID_SELECT_FILE, static_cast<int32_t>(i));
        rowY += rowH + 10;
    }
    (void)contentH;
}

// ============================================================================
// Main Menu & Performance Overlay Framebuffer Rasterizers
// ============================================================================

bool VxrVrMenuUi::RenderMenuToRgba(uint32_t* outRgba, uint32_t width, uint32_t height) {
    if (!outRgba || width < 320 || height < 240) {
        return false;
    }

    m_lastRenderWidth = width;
    m_lastRenderHeight = height;
    m_hitBoxes.clear();

    // Clear entire menu canvas
    FillRect(outRgba, width, height, 0, 0, static_cast<int>(width), static_cast<int>(height), COLOR_BG_OUTER);

    // Render top status bar, bottom status bar, and left navigation sidebar
    RenderTopBar(outRgba, width, height);
    RenderBottomBar(outRgba, width, height);
    RenderSidebar(outRgba, width, height);

    // Active tab content area
    int contentX = static_cast<int>(static_cast<float>(width) * 0.18f);
    int contentY = 36;
    int contentW = static_cast<int>(width) - contentX;
    int contentH = static_cast<int>(height) - 36 - 34;

    switch (m_activeTab) {
        case VrMenuTab::COMPUTERS:
            RenderComputersTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
        case VrMenuTab::ENVIRONMENTS:
            RenderEnvironmentsTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
        case VrMenuTab::GAMES:
            RenderGamesTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
        case VrMenuTab::INPUT:
            RenderInputTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
        case VrMenuTab::SETTINGS:
            RenderSettingsTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
        case VrMenuTab::STREAMING:
            RenderStreamingTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
        case VrMenuTab::VIDEOS:
            RenderVideosTab(outRgba, width, height, contentX, contentY, contentW, contentH);
            break;
    }

    // Draw outer border and VR laser pointer cursor dot
    DrawRectBorder(outRgba, width, height, 0, 0, static_cast<int>(width), static_cast<int>(height), 2, COLOR_BORDER);

    int px = static_cast<int>(Clamp01(m_pointerX) * static_cast<float>(width - 1));
    int py = static_cast<int>(Clamp01(m_pointerY) * static_cast<float>(height - 1));
    DrawCircleFill(outRgba, width, height, px, py, 5, COLOR_ACCENT_BLUE);
    DrawCircleOutline(outRgba, width, height, px, py, 6, COLOR_TEXT_PRIMARY);

    return true;
}

bool VxrVrMenuUi::RenderPerformanceOverlayToRgba(uint32_t* outRgba, uint32_t width, uint32_t height, const VrPerfStats& stats) {
    if (!outRgba || width < 120 || height < 60) {
        return false;
    }

    // Semi-transparent dark HUD panel background
    FillRect(outRgba, width, height, 0, 0, static_cast<int>(width), static_cast<int>(height), 0xE612151Cu);
    DrawRectBorder(outRgba, width, height, 0, 0, static_cast<int>(width), static_cast<int>(height), 2, COLOR_ACCENT_BLUE);

    int x = 14;
    int y = 12;
    char line[160];

    std::snprintf(line, sizeof(line), "PERFORMANCE OVERLAY  |  %s  |  %s", stats.soc_name.c_str(), stats.codec_name.c_str());
    DrawText5x7(outRgba, width, height, x, y, line, 1, COLOR_ACCENT_BLUE);
    y += 18;

    std::snprintf(line, sizeof(line), "FPS: %u   |   Total Latency: %.1f ms   |   Bitrate: %u Mbps",
                  stats.fps, stats.total_latency_ms, stats.bitrate_mbps);
    DrawText5x7(outRgba, width, height, x, y, line, 1, COLOR_TEXT_PRIMARY);
    y += 18;

    std::snprintf(line, sizeof(line), "Game: %.1f ms   Encoding: %.1f ms   Networking: %.1f ms   Decoding: %.1f ms",
                  stats.game_ms, stats.encoding_ms, stats.networking_ms, stats.decoding_ms);
    DrawText5x7(outRgba, width, height, x, y, line, 1, COLOR_TEXT_SECONDARY);
    y += 18;

    std::snprintf(line, sizeof(line), "Network: %s   |   SSW: %s   |   Quality: %s (%ux%u)",
                  stats.wifi_info.c_str(),
                  stats.ssw_active ? "Active" : "Inactive",
                  GetVrQualityName(m_settings.vr_graphics_quality),
                  GetVrRenderWidth(),
                  GetVrRenderHeight());
    DrawText5x7(outRgba, width, height, x, y, line, 1, stats.ssw_active ? COLOR_STATUS_ORANGE : COLOR_STATUS_GREEN);

    return true;
}

// ============================================================================
// Interactive Laser Pointer Hit-Testing
// ============================================================================

void VxrVrMenuUi::OnPointerMove(float normX, float normY) {
    m_pointerX = Clamp01(normX);
    m_pointerY = Clamp01(normY);
}

bool VxrVrMenuUi::OnPointerClick(float normX, float normY) {
    m_pointerX = Clamp01(normX);
    m_pointerY = Clamp01(normY);

    // Ensure hitboxes are populated even if OnPointerClick is called before RenderMenuToRgba
    if (m_hitBoxes.empty()) {
        std::vector<uint32_t> scratch(1280 * 800, 0);
        RenderMenuToRgba(scratch.data(), 1280, 800);
    }

    // Check registered hitboxes in reverse order (topmost first)
    for (int i = static_cast<int>(m_hitBoxes.size()) - 1; i >= 0; --i) {
        const VrUiHitBox& box = m_hitBoxes[static_cast<size_t>(i)];
        if (m_pointerX >= box.x0 && m_pointerX <= box.x1 &&
            m_pointerY >= box.y0 && m_pointerY <= box.y1) {
            return HandleHitAction(box, m_pointerX, m_pointerY);
        }
    }

    // Fallback sidebar check if clicked inside left sidebar region (normX < 0.18f)
    if (m_pointerX < 0.18f) {
        if (m_pointerY >= 0.06f && m_pointerY < 0.45f) {
            int idx = static_cast<int>((m_pointerY - 0.06f) / 0.055f);
            idx = std::max(0, std::min(6, idx));
            SetActiveTab(static_cast<VrMenuTab>(idx));
            return true;
        } else if (m_pointerY >= 0.80f && m_pointerY < 0.88f) {
            m_settings.launch_steamvr_requested = true;
            MarkSettingsDirty();
            return true;
        } else if (m_pointerY >= 0.88f && m_pointerY <= 0.96f) {
            m_settings.quit_requested = true;
            MarkSettingsDirty();
            return true;
        }
    }

    return false;
}

bool VxrVrMenuUi::HandleHitAction(const VrUiHitBox& box, float normX, float normY) {
    (void)normY;
    float sliderRatio = 0.0f;
    if (box.x1 > box.x0) {
        sliderRatio = Clamp01((normX - box.x0) / (box.x1 - box.x0));
    }

    switch (box.id) {
        // Sidebar Navigation Tabs
        case HIT_TAB_COMPUTERS:    SetActiveTab(VrMenuTab::COMPUTERS);    return true;
        case HIT_TAB_ENVIRONMENTS: SetActiveTab(VrMenuTab::ENVIRONMENTS); return true;
        case HIT_TAB_GAMES:        SetActiveTab(VrMenuTab::GAMES);        return true;
        case HIT_TAB_INPUT:        SetActiveTab(VrMenuTab::INPUT);        return true;
        case HIT_TAB_SETTINGS:     SetActiveTab(VrMenuTab::SETTINGS);     return true;
        case HIT_TAB_STREAMING:    SetActiveTab(VrMenuTab::STREAMING);    return true;
        case HIT_TAB_VIDEOS:       SetActiveTab(VrMenuTab::VIDEOS);       return true;

        case HIT_BTN_STEAMVR:
            m_settings.launch_steamvr_requested = true;
            MarkSettingsDirty();
            return true;
        case HIT_BTN_QUIT:
            m_settings.quit_requested = true;
            MarkSettingsDirty();
            return true;
        case HIT_BTN_VIEW_DESKTOP:
            m_visible = false;
            return true;

        // COMPUTERS
        case HIT_COMPUTER_TOGGLE_CONNECT:
            if (box.param >= 0 && static_cast<size_t>(box.param) < m_computers.size()) {
                VrComputerEntry& pc = m_computers[static_cast<size_t>(box.param)];
                pc.connected = !pc.connected;
                m_settings.is_pc_connected = pc.connected;
                if (pc.connected) {
                    m_settings.connected_hostname = pc.hostname;
                    m_settings.local_ip = pc.ip_address;
                }
                MarkSettingsDirty();
                return true;
            }
            break;

        // ENVIRONMENTS
        case HIT_ENV_SELECT:
            m_settings.selected_environment = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_ENV_SLIDER_DISTANCE:
            m_settings.screen_distance_m = 0.8f + sliderRatio * (6.0f - 0.8f);
            MarkSettingsDirty();
            return true;
        case HIT_ENV_SLIDER_SIZE:
            m_settings.screen_size_scale = 0.5f + sliderRatio * (3.0f - 0.5f);
            MarkSettingsDirty();
            return true;
        case HIT_ENV_SLIDER_CURVE:
            m_settings.screen_curvature = sliderRatio;
            MarkSettingsDirty();
            return true;

        // GAMES
        case HIT_GAME_LAUNCH:
            m_settings.launched_game_index = box.param;
            MarkSettingsDirty();
            return true;

        // INPUT
        case HIT_IN_CONTROLLERS_INTERACT:
            m_settings.controllers_interact_desktop = !m_settings.controllers_interact_desktop;
            MarkSettingsDirty();
            return true;
        case HIT_IN_HANDS_INTERACT:
            m_settings.hands_interact_desktop = !m_settings.hands_interact_desktop;
            MarkSettingsDirty();
            return true;
        case HIT_IN_POINTER_STABILIZATION:
            m_settings.pointer_stabilization = !m_settings.pointer_stabilization;
            MarkSettingsDirty();
            return true;
        case HIT_IN_AUTO_HIDE_CONTROLLERS:
            m_settings.auto_hide_controllers = !m_settings.auto_hide_controllers;
            MarkSettingsDirty();
            return true;
        case HIT_IN_THUMB_VERT_SCROLL:
            m_settings.thumbstick_vertical_scroll = !m_settings.thumbstick_vertical_scroll;
            MarkSettingsDirty();
            return true;
        case HIT_IN_THUMB_HORIZ_SCROLL:
            m_settings.thumbstick_horizontal_scroll = !m_settings.thumbstick_horizontal_scroll;
            MarkSettingsDirty();
            return true;
        case HIT_IN_HAND_TRACKING:
            m_settings.hand_tracking = !m_settings.hand_tracking;
            MarkSettingsDirty();
            return true;
        case HIT_IN_GRIP_GRAB_RESIZE:
            m_settings.press_grip_to_grab_resize = !m_settings.press_grip_to_grab_resize;
            MarkSettingsDirty();
            return true;
        case HIT_IN_HOLD_MENU_SWITCH:
            m_settings.hold_menu_to_switch_out_vr = !m_settings.hold_menu_to_switch_out_vr;
            MarkSettingsDirty();
            return true;
        case HIT_IN_EMULATE_GAMEPAD:
            m_settings.emulate_gamepad_on_pc = !m_settings.emulate_gamepad_on_pc;
            MarkSettingsDirty();
            return true;
        case HIT_IN_TOUCH_AS_GAMEPAD:
            m_settings.use_touch_controllers_as_gamepad = !m_settings.use_touch_controllers_as_gamepad;
            MarkSettingsDirty();
            return true;
        case HIT_IN_DPAD_RIGHT_GRIP:
            m_settings.dpad_when_right_grip_pressed = !m_settings.dpad_when_right_grip_pressed;
            MarkSettingsDirty();
            return true;
        case HIT_IN_DPAD_RIGHT_THUMB:
            m_settings.dpad_when_right_thumbstick_pressed = !m_settings.dpad_when_right_thumbstick_pressed;
            MarkSettingsDirty();
            return true;
        case HIT_IN_DPAD_RIGHT_THUMBREST:
            m_settings.dpad_when_right_thumbrest_active = !m_settings.dpad_when_right_thumbrest_active;
            MarkSettingsDirty();
            return true;
        case HIT_IN_CREATE_DESK_PORTAL:
            m_settings.create_desk_portal_requested = true;
            MarkSettingsDirty();
            return true;

        // SETTINGS
        case HIT_SET_AUTO_CONNECT:
            m_settings.auto_connect = !m_settings.auto_connect;
            MarkSettingsDirty();
            return true;
        case HIT_SET_SHOW_GAMES:
            m_settings.show_games_on_connect = !m_settings.show_games_on_connect;
            MarkSettingsDirty();
            return true;
        case HIT_SET_OPTIMAL_RES:
            m_settings.use_optimal_resolution = !m_settings.use_optimal_resolution;
            MarkSettingsDirty();
            return true;
        case HIT_SET_ARRANGE_ON_RECENTER:
            m_settings.arrange_monitors_on_recenter = !m_settings.arrange_monitors_on_recenter;
            MarkSettingsDirty();
            return true;
        case HIT_SET_ARRANGE_NOW:
            m_settings.arrange_monitors_requested = true;
            MarkSettingsDirty();
            return true;
        case HIT_SET_ENV_QUALITY:
            m_settings.environment_quality = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_SET_FRAME_RATE:
            m_settings.desktop_frame_rate = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_SET_DESKTOP_BITRATE:
            m_settings.desktop_bitrate_mbps = static_cast<uint32_t>(std::round(10.0f + sliderRatio * 190.0f));
            MarkSettingsDirty();
            return true;
        case HIT_SET_CONFIG_TRANSPARENCY:
            m_settings.configure_transparency_requested = true;
            MarkSettingsDirty();
            return true;
        case HIT_SET_BRIGHTNESS:
            m_settings.screen_brightness_pct = static_cast<uint32_t>(std::round(10.0f + sliderRatio * 90.0f));
            MarkSettingsDirty();
            return true;
        case HIT_SET_DYNAMIC_LIGHTING:
            m_settings.dynamic_lighting = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_SET_BG_MUSIC:
            m_settings.bg_music_when_disconnected = !m_settings.bg_music_when_disconnected;
            MarkSettingsDirty();
            return true;
        case HIT_SET_MIC_PASSTHROUGH:
            m_settings.microphone_passthrough = !m_settings.microphone_passthrough;
            MarkSettingsDirty();
            return true;
        case HIT_SET_MIC_VOLUME:
            m_settings.mic_volume_pct = static_cast<uint32_t>(std::round(sliderRatio * 100.0f));
            MarkSettingsDirty();
            return true;
        case HIT_SET_NOISE_CANCEL:
            m_settings.noise_cancellation = !m_settings.noise_cancellation;
            MarkSettingsDirty();
            return true;
        case HIT_SET_CUSTOM_ORIENTATION:
            m_settings.allow_custom_orientation = !m_settings.allow_custom_orientation;
            MarkSettingsDirty();
            return true;
        case HIT_SET_BOOST_CLOCKS:
            m_settings.boost_clock_rates = !m_settings.boost_clock_rates;
            MarkSettingsDirty();
            return true;
        case HIT_SET_COPY_SCREENSHOTS:
            m_settings.copy_screenshots_to_desktop = !m_settings.copy_screenshots_to_desktop;
            MarkSettingsDirty();
            return true;
        case HIT_SET_COLOR_VIBRANCE:
            m_settings.increase_color_vibrance = !m_settings.increase_color_vibrance;
            MarkSettingsDirty();
            return true;
        case HIT_SET_REMOVE_HEAD_DELAY:
            m_settings.remove_head_lock_delay = !m_settings.remove_head_lock_delay;
            MarkSettingsDirty();
            return true;

        // STREAMING
        case HIT_STR_VR_QUALITY:
            m_settings.vr_graphics_quality = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_STR_VR_FPS:
            m_settings.vr_frame_rate = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_STR_VR_BITRATE:
            m_settings.vr_bitrate_mbps = static_cast<uint32_t>(std::round(30.0f + sliderRatio * 470.0f));
            MarkSettingsDirty();
            return true;
        case HIT_STR_SHARPENING:
            m_settings.sharpening_pct = static_cast<uint32_t>(std::round(sliderRatio * 100.0f));
            MarkSettingsDirty();
            return true;
        case HIT_STR_PASS_ENV:
            m_settings.passthrough_environment = !m_settings.passthrough_environment;
            MarkSettingsDirty();
            return true;
        case HIT_STR_PASS_HANDS:
            m_settings.passthrough_hands = !m_settings.passthrough_hands;
            MarkSettingsDirty();
            return true;
        case HIT_STR_PASS_PORTAL:
            m_settings.passthrough_desk_portal = !m_settings.passthrough_desk_portal;
            MarkSettingsDirty();
            return true;
        case HIT_STR_CONFIG_PASSTHROUGH:
            m_settings.configure_passthrough_requested = true;
            MarkSettingsDirty();
            return true;
        case HIT_STR_GAMMA:
            m_settings.gamma = 0.50f + sliderRatio * 1.00f;
            MarkSettingsDirty();
            return true;
        case HIT_STR_SSW_MODE:
            m_settings.ssw_mode = static_cast<uint32_t>(box.param);
            MarkSettingsDirty();
            return true;
        case HIT_STR_SGSR:
            m_settings.snapdragon_sgsr = !m_settings.snapdragon_sgsr;
            MarkSettingsDirty();
            return true;
        case HIT_STR_VIDEO_BUFFERING:
            m_settings.video_buffering = !m_settings.video_buffering;
            MarkSettingsDirty();
            return true;
        case HIT_STR_CENTER_PLAY_SPACE:
            m_settings.center_to_play_space = !m_settings.center_to_play_space;
            MarkSettingsDirty();
            return true;
        case HIT_STR_TRACK_CONTROLLERS:
            m_settings.track_controllers = !m_settings.track_controllers;
            MarkSettingsDirty();
            return true;
        case HIT_STR_FORWARD_TRACKING:
            m_settings.forward_tracking_to_pc = !m_settings.forward_tracking_to_pc;
            MarkSettingsDirty();
            return true;
        case HIT_STR_EMULATE_VIVE:
            m_settings.emulate_vive_trackers = !m_settings.emulate_vive_trackers;
            MarkSettingsDirty();
            return true;
        case HIT_STR_EMULATE_INDEX:
            m_settings.emulate_index_controllers = !m_settings.emulate_index_controllers;
            MarkSettingsDirty();
            return true;
        case HIT_STR_COLOR_VIBRANCE:
            m_settings.streaming_color_vibrance = !m_settings.streaming_color_vibrance;
            MarkSettingsDirty();
            return true;
        case HIT_STR_PERF_OVERLAY:
            m_settings.show_performance_overlay = !m_settings.show_performance_overlay;
            MarkSettingsDirty();
            return true;
        case HIT_STR_RESET_DEFAULTS:
            m_settings.ResetStreamingToDefaults();
            MarkSettingsDirty();
            return true;

        // VIDEOS
        case HIT_VID_SELECT_FILE:
            m_settings.selected_video_index = box.param;
            MarkSettingsDirty();
            return true;
        case HIT_VID_SELECT_PROJECTION:
            m_settings.active_video_projection = static_cast<VrVideoProjectionMode>(box.param);
            MarkSettingsDirty();
            return true;

        default:
            break;
    }

    return false;
}

} // namespace vxr
