#ifndef VXR_VR_MENU_UI_H
#define VXR_VR_MENU_UI_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace vxr {

/// Virtual Desktop In-VR Menu Navigation Tabs
enum class VrMenuTab : uint32_t {
    COMPUTERS    = 0,
    ENVIRONMENTS = 1,
    GAMES        = 2,
    INPUT        = 3,
    SETTINGS     = 4,
    STREAMING    = 5,
    VIDEOS       = 6
};

/// Discovered / Connected PC Host Entry (COMPUTERS Tab)
struct VrComputerEntry {
    std::string hostname;
    std::string ip_address;
    bool connected;
    bool available;
    uint32_t link_speed_mbps;
    uint32_t wifi_band_ghz;
};

/// Detected PC VR Game Entry (GAMES Tab)
struct VrGameEntry {
    std::string title;
    std::string app_id;
    bool is_installed;
};

/// Video Playback Projection Mode (VIDEOS Tab)
enum class VrVideoProjectionMode : uint32_t {
    FLAT_2D = 0,
    SBS_3D  = 1,
    VR_180  = 2,
    VR_360  = 3
};

/// Media File Entry from PC MEDIA directory (VIDEOS Tab)
struct VrVideoEntry {
    std::string filename;
    std::string duration_str;
    VrVideoProjectionMode projection_mode;
};

/// Real-Time Performance Overlay HUD Statistics
struct VrPerfStats {
    uint32_t fps = 90;
    float total_latency_ms = 24.5f;
    float game_ms = 4.2f;
    float encoding_ms = 3.8f;
    float networking_ms = 4.1f;
    float decoding_ms = 4.6f;
    uint32_t bitrate_mbps = 150;
    bool ssw_active = false;
    std::string codec_name = "HEVC 10-bit";
    std::string soc_name = "XR2 Gen 1";
    std::string wifi_info = "5 GHz | 1,200 Mbps";
};

/// Complete Virtual Desktop In-VR Menu & Overlay Settings State
struct VrMenuSettings {
    // --- COMPUTERS & CONNECTION ---
    std::string connected_hostname = "DESKTOP-VXR-HOST";
    std::string local_ip = "0.0.0.0";
    uint32_t wifi_band_ghz = 5;
    uint32_t wifi_link_mbps = 1200;
    bool is_pc_connected = false;
    int32_t connect_computer_index = -1;
    bool disconnect_computer_requested = false;

    // --- ENVIRONMENTS ---
    // 0=Home Theater, 1=Modern Apartment, 2=Cinema Auditorium, 3=Gaming Room, 4=Passthrough Void
    uint32_t selected_environment = 0;
    float screen_distance_m = 2.2f;       // 0.8 .. 6.0 m
    float screen_size_scale = 1.25f;      // 0.5 .. 3.0x
    float screen_curvature = 0.35f;       // 0.0 .. 1.0

    // --- INPUT TAB (Screenshot 2) ---
    // Input Options (9 checkboxes)
    bool controllers_interact_desktop = true;
    bool hands_interact_desktop = true;
    bool pointer_stabilization = true;
    bool auto_hide_controllers = true;
    bool thumbstick_vertical_scroll = true;
    bool thumbstick_horizontal_scroll = true;
    bool hand_tracking = true;
    bool press_grip_to_grab_resize = true;
    bool hold_menu_to_switch_out_vr = true;

    // Gamepad (2 checkboxes)
    bool emulate_gamepad_on_pc = true;
    bool use_touch_controllers_as_gamepad = false;

    // Emulate D-pad and Start button (3 checkboxes)
    bool dpad_when_right_grip_pressed = false;
    bool dpad_when_right_thumbstick_pressed = false;
    bool dpad_when_right_thumbrest_active = false;

    // Action requests
    bool create_desk_portal_requested = false;

    // --- SETTINGS TAB (Screenshot 3) ---
    // Computers
    bool auto_connect = true;
    bool show_games_on_connect = false;
    bool use_optimal_resolution = true;
    bool arrange_monitors_on_recenter = false;
    bool arrange_monitors_requested = false;

    // Environment Quality: 0=Low, 1=Medium, 2=High (default Low=0)
    uint32_t environment_quality = 0;

    // Frame Rate: 60, 72, 80, 90, 96, 100, 120 fps (default 90)
    uint32_t desktop_frame_rate = 90;

    // Desktop Bitrate: 10..200 Mbps (default 120 Mbps)
    uint32_t desktop_bitrate_mbps = 120;

    // Video & Screen Transparency
    bool configure_transparency_requested = false;

    // Screen Brightness: 10..100 % (default 100 %)
    uint32_t screen_brightness_pct = 100;

    // Dynamic Lighting: 0=Disabled, 1=Enabled when controller inactive, 2=Always enabled (default 2)
    uint32_t dynamic_lighting = 2;

    // Audio
    bool bg_music_when_disconnected = false;
    bool microphone_passthrough = true;
    uint32_t mic_volume_pct = 71;
    bool noise_cancellation = true;

    // Advanced Options (Settings)
    bool allow_custom_orientation = true;
    bool boost_clock_rates = true;
    bool copy_screenshots_to_desktop = true;
    bool increase_color_vibrance = true;
    bool remove_head_lock_delay = true;

    // --- STREAMING TAB (Screenshot 4) ---
    // VR Graphics Quality:
    // 0=Potato (1440x1536), 1=Low (1728x1824), 2=Medium (2016x2112),
    // 3=High (2496x2592, RTX 3070 / RX 6800 XT), 4=Ultra (2688x2784), 5=Godlike (3072x3216)
    uint32_t vr_graphics_quality = 3;

    // VR Frame Rate: 60, 72, 80, 90, 96, 100, 120 fps (default 90)
    uint32_t vr_frame_rate = 90;

    // VR Bitrate: 30..500 Mbps (default 150 Mbps)
    uint32_t vr_bitrate_mbps = 150;

    // Sharpening: 0..100 % (default 10 %)
    uint32_t sharpening_pct = 10;

    // VR Passthrough
    bool passthrough_environment = false;
    bool passthrough_hands = false;
    bool passthrough_desk_portal = false;
    bool configure_passthrough_requested = false;

    // Gamma: 0.50..1.50 (default 1.00)
    float gamma = 1.00f;

    // Synchronous Spacewarp (SSW): 0=Disabled, 1=Always enabled, 2=Automatic (default 0)
    uint32_t ssw_mode = 0;

    // Advanced Options (Streaming)
    bool snapdragon_sgsr = true;
    bool video_buffering = false;
    bool center_to_play_space = true;
    bool track_controllers = true;
    bool forward_tracking_to_pc = true;
    bool emulate_vive_trackers = false;
    bool emulate_index_controllers = false;
    bool streaming_color_vibrance = true;
    bool show_performance_overlay = true;

    // --- VIDEOS TAB ---
    VrVideoProjectionMode active_video_projection = VrVideoProjectionMode::FLAT_2D;
    int32_t selected_video_index = 0;
    int32_t launched_game_index = -1;

    // --- SIDEBAR & STATUS BAR ---
    bool launch_steamvr_requested = false;
    bool quit_requested = false;
    std::string headset_model = "Meta Quest";
    std::string clock_text = "12:00 PM";
    uint32_t battery_pct = 100;

    // Dirty flag for syncing with PC Host
    bool settingsDirty = false;

    /// Resets all settings to Virtual Desktop factory defaults
    void ResetToDefaults();

    /// Resets only the Streaming tab settings to defaults
    void ResetStreamingToDefaults();
};

/// Interactive Hit Region registered during layout/render
struct VrUiHitBox {
    uint32_t id;
    float x0, y0, x1, y1; // Normalized 0.0 .. 1.0 coordinates
    int32_t param;
};

/// Virtual Desktop In-VR Menu & Overlay System Engine
class VxrVrMenuUi {
public:
    VxrVrMenuUi();
    ~VxrVrMenuUi();

    /// Initializes the menu with default computers, games, and media lists
    bool Initialize();

    /// Visibility & Tab management
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const { return m_visible; }
    void ToggleVisible() { m_visible = !m_visible; }

    void SetActiveTab(VrMenuTab tab);
    VrMenuTab GetActiveTab() const { return m_activeTab; }

    /// Access & modify underlying settings
    VrMenuSettings& GetSettings() { return m_settings; }
    const VrMenuSettings& GetSettings() const { return m_settings; }
    VrMenuSettings& GetSettingsMutable() { return m_settings; }

    bool IsSettingsDirty() const { return m_settingsDirty || m_settings.settingsDirty; }
    void ClearSettingsDirty() { m_settingsDirty = false; m_settings.settingsDirty = false; }
    void MarkSettingsDirty() { m_settingsDirty = true; m_settings.settingsDirty = true; }

    /// Returns exact per-eye VR render width for the selected VR Graphics Quality preset
    uint32_t GetVrRenderWidth() const;

    /// Returns exact per-eye VR render height for the selected VR Graphics Quality preset
    uint32_t GetVrRenderHeight() const;

    /// Static helpers for VR Graphics Quality preset resolution lookup (0..5)
    static uint32_t GetVrRenderWidthForQuality(uint32_t quality_index);
    static uint32_t GetVrRenderHeightForQuality(uint32_t quality_index);
    static const char* GetVrQualityName(uint32_t quality_index);

    /// Updates discovered PCs, VR games, and media files
    void SetComputers(const std::vector<VrComputerEntry>& computers);
    const std::vector<VrComputerEntry>& GetComputers() const { return m_computers; }
    std::vector<VrComputerEntry>& GetComputersMutable() { return m_computers; }

    void SetGames(const std::vector<VrGameEntry>& games);
    const std::vector<VrGameEntry>& GetGames() const { return m_games; }

    void SetVideos(const std::vector<VrVideoEntry>& videos);
    const std::vector<VrVideoEntry>& GetVideos() const { return m_videos; }

    /// Interactive Laser Pointer Hit-Testing (normX, normY in [0.0, 1.0])
    void OnPointerMove(float normX, float normY);
    bool OnPointerClick(float normX, float normY);

    float GetPointerX() const { return m_pointerX; }
    float GetPointerY() const { return m_pointerY; }

    /// Renders the complete Virtual Desktop In-VR Menu (active tab, sidebar, top & bottom status bars)
    /// into an RGBA8888 pixel buffer (e.g. 1280x800) ready for Vulkan texture upload
    bool RenderMenuToRgba(uint32_t* outRgba, uint32_t width, uint32_t height);

    /// Renders the real-time Performance Overlay HUD into an RGBA8888 pixel buffer
    bool RenderPerformanceOverlayToRgba(uint32_t* outRgba, uint32_t width, uint32_t height, const VrPerfStats& stats);

private:
    // Internal rendering primitives (RGBA8888 where R is low byte, or 0xAABBGGRR / 0xAARRGGBB consistent)
    static void FillRect(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, int h, uint32_t color);
    static void DrawRectBorder(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, int h, int thickness, uint32_t color);
    static void DrawLine(uint32_t* buf, uint32_t bw, uint32_t bh, int x0, int y0, int x1, int y1, uint32_t color);
    static void DrawCircleFill(uint32_t* buf, uint32_t bw, uint32_t bh, int cx, int cy, int radius, uint32_t color);
    static void DrawCircleOutline(uint32_t* buf, uint32_t bw, uint32_t bh, int cx, int cy, int radius, uint32_t color);
    static void DrawChar5x7(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, char c, int scale, uint32_t color);
    static void DrawText5x7(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, const char* text, int scale, uint32_t color);
    static int MeasureTextWidth5x7(const char* text, int scale);

    // UI Widget renderers
    void DrawCheckbox(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, const char* label, bool checked, uint32_t hitId);
    void DrawRadioButton(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, const char* label, bool selected, uint32_t hitId, int32_t param);
    void DrawSlider(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, const char* label, const char* valueText, float normVal, uint32_t hitId);
    void DrawButton(uint32_t* buf, uint32_t bw, uint32_t bh, int x, int y, int w, int h, const char* label, bool primaryBlue, uint32_t hitId, int32_t param = 0);

    // Tab content renderers
    void RenderTopBar(uint32_t* buf, uint32_t bw, uint32_t bh);
    void RenderBottomBar(uint32_t* buf, uint32_t bw, uint32_t bh);
    void RenderSidebar(uint32_t* buf, uint32_t bw, uint32_t bh);
    void RenderComputersTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);
    void RenderEnvironmentsTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);
    void RenderGamesTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);
    void RenderInputTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);
    void RenderSettingsTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);
    void RenderStreamingTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);
    void RenderVideosTab(uint32_t* buf, uint32_t bw, uint32_t bh, int contentX, int contentY, int contentW, int contentH);

    void RegisterHitBox(uint32_t id, int x, int y, int w, int h, uint32_t bw, uint32_t bh, int32_t param = 0);
    bool HandleHitAction(const VrUiHitBox& box, float normX, float normY);

    bool m_visible;
    VrMenuTab m_activeTab;
    VrMenuSettings m_settings;
    bool m_settingsDirty;

    float m_pointerX;
    float m_pointerY;
    uint32_t m_lastRenderWidth;
    uint32_t m_lastRenderHeight;

    std::vector<VrComputerEntry> m_computers;
    std::vector<VrGameEntry> m_games;
    std::vector<VrVideoEntry> m_videos;
    std::vector<VrUiHitBox> m_hitBoxes;
};

} // namespace vxr

#endif // VXR_VR_MENU_UI_H
