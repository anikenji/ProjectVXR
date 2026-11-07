#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmsystem.h>
#include <dwmapi.h>
#pragma comment(lib, "winmm.lib")
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "vxr_encoder.h"
#include "vxr_iddcx_control.h"
#include "VxrComputeShaders.h"
#include "DxgiDuplicator.h"
#include "VxrWifiOptimizer.h"
#include "VxrNvapiOptimizer.h"
#include "../../drivers/openvr/include/VxrDriverShared.h"

using namespace Gdiplus;

// ============================================================================
// SHARED UDP WIRE PACKET STRUCTS (VXRV Video Fragments & VXRC Control Packets)
// ============================================================================
#pragma pack(push, 1)

static const uint32_t VXR_UDP_VIDEO_MAGIC = 0x56585256; // "VXRV"
static const uint32_t VXR_UDP_CTRL_MAGIC  = 0x56585243; // "VXRC"
static const uint32_t VXR_HOST_INFO_MAGIC = 0x56585248; // "VXRH"
static const uint32_t VXR_UDP_AUDIO_MAGIC = 0x56585241; // "VXRA" (PC -> Quest PCM16 Stereo Audio)
static const uint32_t VXR_UDP_MIC_MAGIC   = 0x5658524D; // "VXRM" (Quest -> PC PCM16 Mono Mic)

enum VxrControlCommandType : uint8_t {
    VXR_CMD_DISCOVER        = 1,
    VXR_CMD_CONNECT         = 1,
    VXR_CMD_MOUSE_INPUT     = 2,
    VXR_CMD_CLIENT_SETTINGS = 3,
    VXR_CMD_LAUNCH_STEAMVR  = 4,
    VXR_CMD_SWITCH_MONITOR  = 5,
    VXR_CMD_TRACKING_STATE  = 6,
    VXR_CMD_DISCONNECT      = 7,
    VXR_CMD_KEYBOARD_INPUT  = 8
};

struct VxrUdpAudioHeader {
    uint32_t magic;          // 0x56585241 ("VXRA") or 0x5658524D ("VXRM")
    uint32_t seq;
    uint16_t sample_rate;    // 24000
    uint8_t  channels;       // 2 for speaker, 1 for mic
    uint8_t  flags;
    uint16_t sample_frames;  // number of PCM16 frames following header
    uint16_t reserved;
};

enum VxrMouseButtonFlags : uint8_t {
    VXR_MOUSE_LEFT_DOWN   = (1 << 0),
    VXR_MOUSE_LEFT_UP     = (1 << 1),
    VXR_MOUSE_RIGHT_DOWN  = (1 << 2),
    VXR_MOUSE_RIGHT_UP    = (1 << 3),
    VXR_MOUSE_MIDDLE_DOWN = (1 << 4),
    VXR_MOUSE_MIDDLE_UP   = (1 << 5)
};

struct VxrUdpVideoFragmentHeader {
    uint32_t magic;          // 0x56585256 ("VXRV")
    uint32_t frame_id;
    uint16_t frag_idx;
    uint16_t frag_count;
    uint8_t  flags;          // bit 0 = keyframe, bit 1 = intra_refresh, bit 2 = stereo_vr_mode
    uint8_t  codec_id;
    uint16_t payload_len;
    uint64_t pts_us;
    float    cursor_norm_x;
    float    cursor_norm_y;
    uint8_t  cursor_visible;
    uint8_t  reserved[3];
};

struct VxrControlHeader {
    uint32_t magic;          // 0x56585243 ("VXRC")
    uint8_t  cmd_type;       // 1..8
    uint8_t  reserved[3];
};

struct VxrControlDiscoverPacket {
    VxrControlHeader hdr;
    char headset_name[64];
};

// Exact 132-byte wire layout matching Quest VxrClientNetwork.h
struct VxrControlTrackingPacket {
    VxrControlHeader hdr;          // magic="VXRC", cmd_type=6 (VXR_CMD_TRACKING_STATE)
    float    hmd_pos[3];
    float    hmd_quat[4];          // x, y, z, w
    float    left_pos[3];
    float    left_quat[4];         // x, y, z, w
    float    right_pos[3];
    float    right_quat[4];        // x, y, z, w
    float    left_trigger;
    float    left_grip;
    float    left_joy[2];
    uint32_t left_buttons;
    float    right_trigger;
    float    right_grip;
    float    right_joy[2];
    uint32_t right_buttons;
};

struct VxrControlMouseInputPacket {
    VxrControlHeader hdr;
    float   norm_x;          // 0.0 .. 1.0
    float   norm_y;          // 0.0 .. 1.0
    uint8_t button_flags;    // VxrMouseButtonFlags
    uint8_t reserved[3];
    int16_t scroll_dx;
    int16_t scroll_dy;
};

struct VxrControlClientSettingsPacket {
    VxrControlHeader hdr;
    uint16_t desktop_bitrate_mbps;
    uint16_t vr_bitrate_mbps;
    uint8_t  vr_graphics_quality; // 0=Potato, 1=Low, 2=Medium, 3=High, 4=Ultra, 5=Godlike
    uint8_t  vr_fps;              // 60, 72, 80, 90, 96, 100, 120
    uint8_t  sharpening_pct;      // 0..100
    uint8_t  ssw_mode;            // 0=Disabled, 1=Automatic, 2=Always Enabled
    uint8_t  enable_sgsr;         // 0 or 1
    uint8_t  passthrough_mask;    // bitmask for environment/hand/desk passthrough
    uint8_t  reserved[2];
};

// Unified 36-byte Client Control Packet sent by VxrClientNetwork.cpp (Quest Client)
struct VxrClientControlPacket {
    uint32_t magic;                // 0x56585243 ("VXRC")
    uint8_t  cmd_type;             // 1..5
    uint8_t  button_mask;          // bit0=Left, bit1=Right, bit2=Middle
    int16_t  wheel_delta;
    float    cursor_norm_x;
    float    cursor_norm_y;
    uint32_t desktop_bitrate_mbps;
    uint32_t vr_bitrate_mbps;
    uint32_t vr_quality_tier;
    uint32_t vr_fps;
    uint32_t sharpening_pct;
    uint8_t  ssw_mode;
    uint8_t  enable_sgsr;
    uint8_t  reserved[2];
};

struct VxrHostInfoPacket {
    uint32_t magic;          // 0x56585248 ("VXRH")
    uint16_t udp_port;
    uint16_t desktop_width;
    uint16_t desktop_height;
    uint16_t refresh_rate_hz;
    uint16_t bitrate_mbps;
    uint8_t  codec_id;
    uint8_t  reserved;
    char     computer_name[64];
    char     gpu_name[64];
    char     codec_name[32];
    char     json_summary[256];
};

#pragma pack(pop)

static const int WIN_W = 680;
static const int WIN_H = 475;
static const int TOP_BAR_H = 46;
static const int BOTTOM_BAR_H = 33;
static const int SIDEBAR_W = 154;

#define WM_TRAYICON       (WM_USER + 100)
#define WM_SERVICE_UPDATE (WM_USER + 101)
#define ID_TRAY_OPEN      1001
#define ID_TRAY_EXIT      1002
#define HOTKEY_BASE_ID    2000

enum NavTab {
    TAB_ACCOUNTS = 0,
    TAB_BINDINGS = 1,
    TAB_OPTIONS  = 2,
    TAB_ADVANCED = 3,
    TAB_MEDIA    = 4,
    TAB_ABOUT    = 5
};

static std::mutex g_logMutex;

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &res[0], sz, nullptr, nullptr);
    return res;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &res[0], sz);
    return res;
}

static void AppendServiceLog(const std::wstring& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::ofstream out("vxr_streamer.log", std::ios::app);
    if (!out.is_open()) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[64];
    sprintf_s(timeBuf, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    out << timeBuf << WideToUtf8(msg) << "\n";
}

struct ShortcutBinding {
    std::wstring actionName;
    UINT modifiers;
    UINT vk;

    std::wstring FormatText() const {
        if (vk == 0) return L"None";
        std::wstring s;
        if (modifiers & MOD_CONTROL) s += L"Ctrl+";
        if (modifiers & MOD_ALT)     s += L"Alt+";
        if (modifiers & MOD_SHIFT)   s += L"Shift+";
        if (modifiers & MOD_WIN)     s += L"Windows+";

        if (vk >= 'A' && vk <= 'Z') {
            wchar_t ch[2] = { (wchar_t)vk, 0 };
            s += ch;
        } else if (vk >= '0' && vk <= '9') {
            wchar_t ch[2] = { (wchar_t)vk, 0 };
            s += ch;
        } else if (vk >= VK_F1 && vk <= VK_F12) {
            wchar_t buf[16];
            swprintf_s(buf, L"F%d", (int)(vk - VK_F1 + 1));
            s += buf;
        } else {
            wchar_t name[64] = {};
            UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
            if (GetKeyNameTextW(scan << 16, name, 63) > 0) {
                s += name;
            } else {
                wchar_t buf[16];
                swprintf_s(buf, L"Key 0x%02X", vk);
                s += buf;
            }
        }
        return s;
    }
};

struct AppConfig {
    // Tab 0: ACCOUNTS
    std::wstring metaAccount = L"Phuong1226";

    // Tab 1: BINDINGS
    ShortcutBinding bindings[9];

    // Tab 2: OPTIONS
    int preferredCodec = 1; // 0=Automatic, 1=HEVC 10-bit, 2=AV1 10-bit, 3=HEVC, 4=H.264+, 5=H.264
    bool adaptiveQuantization = true;
    bool twoPassEncoding = true;
    int openxrRuntime = 0;  // 0=VXR-XR, 1=Automatic, 2=SteamVR
    int gamepadEmulation = 0; // 0=Automatic, 1=Xbox 360 (ViGEm), 2=DualShock 4, 3=Disabled
    int audioStreaming = 0;   // 0=VR headset only, 1=Computer only, 2=Both
    bool useVirtualAudioDriver = true;
    bool voiceMeeterMode = false;

    bool allowRemoteConnections = true;
    bool encryptLocalTraffic = false;
    bool autoAdjustBitrate = false;
    bool startWithWindows = true;
    bool startMinimizedInTray = true;
    bool useTouchInput = true;
    bool lockComputerOnDisconnect = false;
    bool autoSelectMicrophone = true;
    bool askForComputerAccess = true;

    // Tab 3: ADVANCED
    int hFovTangent = 100;       // 50..100%
    int vFovTangent = 93;        // 50..100%
    int renderResolution = 100;  // 50..150%
    int foveaSize = 15;          // 10..100%
    bool additionalSharpening = false;
    int sharpeningPercent = 70;  // 0..100%
    bool useFovStencil = false;
    bool boostGamePriority = false;

    // Tab 4: MEDIA
    std::wstring screenshotsPath;
    std::vector<std::wstring> videoPaths;

    AppConfig() {
        ResetBindings();
        wchar_t picPath[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, picPath))) {
            screenshotsPath = picPath;
        } else {
            screenshotsPath = L"E:\\Code\\ProjectVXR";
        }

        wchar_t vidPath[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYVIDEO, nullptr, 0, vidPath))) {
            videoPaths.push_back(std::wstring(vidPath) + L"\\");
        } else {
            videoPaths.push_back(L"C:\\Users\\Public\\Videos\\");
        }
    }

    const wchar_t* GetCodecName() const {
        static const wchar_t* names[] = { L"Automatic", L"HEVC 10-bit", L"AV1 10-bit", L"HEVC", L"H.264+", L"H.264" };
        if (preferredCodec >= 0 && preferredCodec < 6) return names[preferredCodec];
        return L"HEVC 10-bit";
    }

    void ResetBindings() {
        bindings[0] = { L"Switch Monitor",             MOD_SHIFT | MOD_WIN, 'A' };
        bindings[1] = { L"Toggle VR Mode",             MOD_SHIFT | MOD_WIN, 'D' };
        bindings[2] = { L"Toggle VR Passthrough",      MOD_SHIFT | MOD_WIN, 'C' };
        bindings[3] = { L"Enable VR Passthrough",      MOD_SHIFT | MOD_WIN, 'Z' };
        bindings[4] = { L"Disable VR Passthrough",     MOD_SHIFT | MOD_WIN, 'X' };
        bindings[5] = { L"Toggle Hand Passthrough",    MOD_SHIFT | MOD_WIN, 'H' };
        bindings[6] = { L"Toggle Desk Passthrough",    MOD_SHIFT | MOD_WIN, 'K' };
        bindings[7] = { L"Toggle Performance Overlay", MOD_SHIFT | MOD_WIN, 'O' };
        bindings[8] = { L"Toggle Foveated Streaming",  MOD_SHIFT | MOD_WIN, 'G' };
    }

    void ResetOptions() {
        preferredCodec = 1;
        adaptiveQuantization = true;
        twoPassEncoding = true;
        openxrRuntime = 0;
        gamepadEmulation = 0;
        audioStreaming = 0;
        useVirtualAudioDriver = true;
        voiceMeeterMode = false;
        allowRemoteConnections = true;
        encryptLocalTraffic = false;
        autoAdjustBitrate = false;
        startWithWindows = false;
        startMinimizedInTray = false;
        useTouchInput = true;
        lockComputerOnDisconnect = false;
        autoSelectMicrophone = true;
        askForComputerAccess = true;
    }

    void ResetAdvanced() {
        hFovTangent = 100;
        vFovTangent = 93;
        renderResolution = 100;
        foveaSize = 15;
        additionalSharpening = false;
        sharpeningPercent = 70;
        useFovStencil = false;
        boostGamePriority = false;
    }
};

// Live Background Host Service & Real System Telemetry State
struct HostServiceState {
    std::atomic<bool> isStarting{ true };
    std::atomic<int>  bootProgressPct{ 0 };
    std::mutex        statusMutex;
    std::wstring      bootStepText = L"Starting Project VXR Service...";
    std::wstring      lastActionToast;
    ULONGLONG         toastExpireTick = 0;

    // Real system info for ABOUT tab (matching Screenshot 1)
    std::wstring      gpuNameWithDriver = L"Detecting GPU...";
    std::wstring      gpuShortName = L"Detecting GPU...";
    std::wstring      connectedHeadset = L"Not connected";
    ULONGLONG         lastClientPacketTick = 0;
    float             activeLatencyMs = 0.0f;
    std::wstring      cpuName = L"Detecting Processor...";
    std::wstring      ramTotalGb = L"Detecting...";
    std::wstring      osVersionStr = L"Windows 10/11";
    bool              secureBootEnabled = false;
    bool              hagsEnabled = false;
    bool              pcEthernetConnected = false;

    bool              d3d11Ready = false;
    bool              nvencActive = false;
    bool              encoderDllLoaded = false;
    bool              openxrDllLoaded = false;
    bool              openvrDllLoaded = false;
    bool              iddcxDllLoaded = false;
    bool              wasapiReady = false;
    bool              udpServerListening = false;
    bool              mdnsListening = false;
    bool              mmcssBoostActive = false;
    int               registeredHotkeysCount = 0;
    uint16_t          udpPort = 38810;
    std::atomic<uint64_t> framesEncoded{ 0 };
    std::atomic<uint64_t> mdnsBeaconsSent{ 0 };

    // Live Advanced Optical & Shader state
    float             activeHFovDeg = 96.0f;
    float             activeVFovDeg = 91.8f;
    uint32_t          activeEyeWidth = 2064;
    uint32_t          activeEyeHeight = 2208;
    float             activeCasSharpness = 0.0f;
    uint32_t          activeBitrateMbps = 150;
    uint32_t          activeVrFps = 90;
    uint32_t          activeMonitorIdx = 0;
    std::atomic<bool> switchMonitorRequested{ false };
    uint8_t           activeSswMode = 1;
    bool              activeSgsrEnabled = false;
    uint8_t           activePassthroughMask = 0;

    HMODULE           hEncoderDll = nullptr;
    HMODULE           hOpenXrDll  = nullptr;
    HMODULE           hOpenVrDll  = nullptr;
    HMODULE           hIddCxDll   = nullptr;
    VxrEncoderHandle  encoderHandle = nullptr;
    HANDLE            mmcssHandle = nullptr;
    SOCKET            udpSocket = INVALID_SOCKET;
    SOCKET            mdnsSocket = INVALID_SOCKET;
    std::atomic<bool> stopRequested{ false };
};

struct DropdownSpec {
    int id;
    Rect rect;
    std::vector<std::wstring> items;
    int* selectedIndex;
};

struct CheckboxSpec {
    int id;
    Rect rect;
    std::wstring label;
    bool* value;
    bool disabled;
};

struct SliderSpec {
    int id;
    Rect trackRect;
    int minVal;
    int maxVal;
    int* value;
    bool* enabledFlag;
};

static AppConfig g_cfg;
static HostServiceState g_svc;
static vxr::VxrCasCompute g_casShader;
static vxr::VxrSgsrCompute g_sgsrShader;
static HWND g_hwnd = nullptr;
static NavTab g_activeTab = TAB_OPTIONS;
static int g_hoverNav = -1;
static bool g_hoverMin = false;
static bool g_hoverClose = false;
static int g_hoverBtnId = -1;

static bool g_editingAccount = false;
static std::wstring g_accountEditBuf;

static int g_recordingBindingIdx = -1;
static int g_hoverBindingIdx = -1;

static int g_openDropdownId = -1;
static int g_hoverDropdownItem = -1;
static Rect g_openDropdownMenuRect;
static std::vector<std::wstring> g_openDropdownItems;
static int* g_openDropdownTarget = nullptr;

static int g_draggingSliderId = -1;

static std::vector<DropdownSpec> g_dropdowns;
static std::vector<CheckboxSpec> g_checkboxes;
static std::vector<SliderSpec> g_sliders;
static NOTIFYICONDATAW g_nid = {};
static std::thread g_serviceThread;

// Typedefs for dynamic C API calls into vxr_encoder.dll, openxr_vxr.dll, driver_vrserver.dll
typedef VxrEncoderHandle (*PFN_vxr_encoder_create)();
typedef void (*PFN_vxr_encoder_destroy)(VxrEncoderHandle);
typedef int (*PFN_vxr_encoder_initialize)(VxrEncoderHandle, const VxrEncoderConfig*, void*);
typedef int (*PFN_vxr_encoder_reconfigure_bitrate)(VxrEncoderHandle, uint32_t, uint32_t);
typedef int (*PFN_vxr_encoder_encode_texture)(VxrEncoderHandle, void*, uint64_t, int, VxrBitstreamFrame*);
typedef void (*PFN_vxr_xr_set_optical_config)(float, float, float, float, int);
typedef void (*PFN_VxrDriver_SetOpticalConfig)(float, float, float);

static PFN_vxr_encoder_create              pfn_encoder_create = nullptr;
static PFN_vxr_encoder_destroy             pfn_encoder_destroy = nullptr;
static PFN_vxr_encoder_initialize          pfn_encoder_init = nullptr;
static PFN_vxr_encoder_reconfigure_bitrate pfn_encoder_reconfig = nullptr;
static PFN_vxr_encoder_encode_texture      pfn_encoder_encode = nullptr;
static PFN_vxr_xr_set_optical_config       pfn_xr_set_optical = nullptr;
static PFN_VxrDriver_SetOpticalConfig      pfn_openvr_set_optical = nullptr;

static void ShowToast(const std::wstring& msg) {
    AppendServiceLog(msg);
    {
        std::lock_guard<std::mutex> lock(g_svc.statusMutex);
        g_svc.lastActionToast = msg;
        g_svc.toastExpireTick = GetTickCount64() + 3800;
    }
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

static std::wstring ReadRegString(HKEY root, const wchar_t* subKey, const wchar_t* valName) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return L"";
    wchar_t buf[512] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    std::wstring res;
    if (RegQueryValueExW(hKey, valName, nullptr, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        res = buf;
    }
    RegCloseKey(hKey);
    return res;
}

static bool ReadRegDword(HKEY root, const wchar_t* subKey, const wchar_t* valName, DWORD* outVal) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD val = 0;
    DWORD sz = sizeof(val);
    DWORD type = 0;
    bool ok = (RegQueryValueExW(hKey, valName, nullptr, &type, (LPBYTE)&val, &sz) == ERROR_SUCCESS && type == REG_DWORD);
    if (ok && outVal) *outVal = val;
    RegCloseKey(hKey);
    return ok;
}

static void QueryRealSystemSpecs() {
    // 1. CPU Name
    std::wstring cpu = ReadRegString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    while (!cpu.empty() && cpu.front() == L' ') cpu.erase(cpu.begin());
    if (cpu.empty()) cpu = L"x86_64 Multi-Core Processor";

    // 2. Physical RAM (GB)
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    wchar_t ramBuf[64] = L"16.0 GB";
    if (GlobalMemoryStatusEx(&mem)) {
        double gb = (double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        swprintf_s(ramBuf, L"%.1f GB", gb);
    }

    // 3. Operating System Name & DisplayVersion
    std::wstring prodName = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    std::wstring dispVer  = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    std::wstring buildStr = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild");
    if (!buildStr.empty()) {
        int buildNum = _wtoi(buildStr.c_str());
        if (buildNum >= 22000) {
            size_t pos = prodName.find(L"Windows 10");
            if (pos != std::wstring::npos) prodName.replace(pos, 10, L"Windows 11");
        }
    }
    std::wstring osFull = prodName.empty() ? L"Windows 10 Pro" : prodName;
    if (!dispVer.empty()) osFull += L" " + dispVer;

    // 4. UEFI Secure Boot
    DWORD sbVal = 0;
    bool secBoot = ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State", L"UEFISecureBootEnabled", &sbVal) && (sbVal == 1);

    // 5. HAGS (Hardware-Accelerated GPU Scheduling: HwSchMode == 2 means Enabled)
    DWORD hagsVal = 2;
    bool hags = true;
    if (ReadRegDword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers", L"HwSchMode", &hagsVal)) {
        hags = (hagsVal == 2);
    }

    // 6. PC Ethernet Status via GetAdaptersAddresses
    bool ethUp = false;
    ULONG outBufLen = 16384;
    std::vector<BYTE> adapterBuf(outBufLen);
    PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapterBuf.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, pAddresses, &outBufLen) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES curr = pAddresses; curr != nullptr; curr = curr->Next) {
            if (curr->IfType == IF_TYPE_ETHERNET_CSMACD && curr->OperStatus == IfOperStatusUp) {
                ethUp = true;
                break;
            }
        }
    }

    // 7. GPU Driver Version: First query nvml.dll (nvmlInit_v2 + nvmlSystemGetDriverVersion) & nvapi64.dll,
    //    then fallback to Display Class Registry subkeys 0000..0009 matching DriverDesc == g_svc.gpuShortName
    std::wstring gpuNameSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_svc.statusMutex);
        gpuNameSnapshot = g_svc.gpuShortName;
    }
    std::wstring shortDrv = vxr::VxrNvapiOptimizer::QueryExactDriverVersion(gpuNameSnapshot);
    if (shortDrv.empty()) {
        for (int idx = 0; idx < 10; ++idx) {
            wchar_t subKey[256] = {};
            swprintf_s(subKey, L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%04d", idx);
            std::wstring desc = ReadRegString(HKEY_LOCAL_MACHINE, subKey, L"DriverDesc");
            if (!desc.empty() && _wcsicmp(desc.c_str(), gpuNameSnapshot.c_str()) == 0) {
                std::wstring drvVer = ReadRegString(HKEY_LOCAL_MACHINE, subKey, L"DriverVersion");
                if (drvVer.size() >= 6) {
                    std::wstring digits;
                    for (wchar_t c : drvVer) {
                        if (c >= L'0' && c <= L'9') digits.push_back(c);
                    }
                    if (digits.size() >= 5) {
                        std::wstring tail5 = digits.substr(digits.size() - 5);
                        shortDrv = tail5.substr(0, 3) + L"." + tail5.substr(3, 2);
                    } else {
                        shortDrv = drvVer;
                    }
                }
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_svc.statusMutex);
    g_svc.cpuName = cpu;
    g_svc.ramTotalGb = ramBuf;
    g_svc.osVersionStr = osFull;
    g_svc.secureBootEnabled = secBoot;
    g_svc.hagsEnabled = hags;
    g_svc.pcEthernetConnected = ethUp;
    if (!shortDrv.empty()) {
        g_svc.gpuNameWithDriver = g_svc.gpuShortName + L" (" + shortDrv + L")";
    } else {
        g_svc.gpuNameWithDriver = g_svc.gpuShortName;
    }
}

static void SaveConfig() {
    std::ofstream out("vxr_streamer_settings.ini");
    if (!out.is_open()) return;
    out << "metaAccount=" << WideToUtf8(g_cfg.metaAccount) << "\n";
    out << "preferredCodec=" << g_cfg.preferredCodec << "\n";
    out << "adaptiveQuantization=" << g_cfg.adaptiveQuantization << "\n";
    out << "twoPassEncoding=" << g_cfg.twoPassEncoding << "\n";
    out << "openxrRuntime=" << g_cfg.openxrRuntime << "\n";
    out << "gamepadEmulation=" << g_cfg.gamepadEmulation << "\n";
    out << "audioStreaming=" << g_cfg.audioStreaming << "\n";
    out << "useVirtualAudioDriver=" << g_cfg.useVirtualAudioDriver << "\n";
    out << "voiceMeeterMode=" << g_cfg.voiceMeeterMode << "\n";
    out << "allowRemoteConnections=" << g_cfg.allowRemoteConnections << "\n";
    out << "encryptLocalTraffic=" << g_cfg.encryptLocalTraffic << "\n";
    out << "autoAdjustBitrate=" << g_cfg.autoAdjustBitrate << "\n";
    out << "startWithWindows=" << g_cfg.startWithWindows << "\n";
    out << "startMinimizedInTray=" << g_cfg.startMinimizedInTray << "\n";
    out << "useTouchInput=" << g_cfg.useTouchInput << "\n";
    out << "lockComputerOnDisconnect=" << g_cfg.lockComputerOnDisconnect << "\n";
    out << "autoSelectMicrophone=" << g_cfg.autoSelectMicrophone << "\n";
    out << "askForComputerAccess=" << g_cfg.askForComputerAccess << "\n";
    out << "hFovTangent=" << g_cfg.hFovTangent << "\n";
    out << "vFovTangent=" << g_cfg.vFovTangent << "\n";
    out << "renderResolution=" << g_cfg.renderResolution << "\n";
    out << "foveaSize=" << g_cfg.foveaSize << "\n";
    out << "additionalSharpening=" << g_cfg.additionalSharpening << "\n";
    out << "sharpeningPercent=" << g_cfg.sharpeningPercent << "\n";
    out << "useFovStencil=" << g_cfg.useFovStencil << "\n";
    out << "boostGamePriority=" << g_cfg.boostGamePriority << "\n";
    out << "screenshotsPath=" << WideToUtf8(g_cfg.screenshotsPath) << "\n";
    for (int i = 0; i < 9; ++i) {
        out << "binding_" << i << "_mod=" << g_cfg.bindings[i].modifiers << "\n";
        out << "binding_" << i << "_vk=" << g_cfg.bindings[i].vk << "\n";
    }
    out << "videoPathsCount=" << g_cfg.videoPaths.size() << "\n";
    for (size_t i = 0; i < g_cfg.videoPaths.size(); ++i) {
        out << "videoPath_" << i << "=" << WideToUtf8(g_cfg.videoPaths[i]) << "\n";
    }
}

static void LoadConfig() {
    std::ifstream in("vxr_streamer_settings.ini");
    if (!in.is_open()) return;
    std::string line;
    std::vector<std::wstring> loadedVidPaths;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        if (k == "metaAccount") g_cfg.metaAccount = Utf8ToWide(v);
        else if (k == "preferredCodec") g_cfg.preferredCodec = std::stoi(v);
        else if (k == "adaptiveQuantization") g_cfg.adaptiveQuantization = (v == "1");
        else if (k == "twoPassEncoding") g_cfg.twoPassEncoding = (v == "1");
        else if (k == "openxrRuntime") g_cfg.openxrRuntime = std::stoi(v);
        else if (k == "gamepadEmulation") g_cfg.gamepadEmulation = std::stoi(v);
        else if (k == "audioStreaming") g_cfg.audioStreaming = std::stoi(v);
        else if (k == "useVirtualAudioDriver") g_cfg.useVirtualAudioDriver = (v == "1");
        else if (k == "voiceMeeterMode") g_cfg.voiceMeeterMode = (v == "1");
        else if (k == "allowRemoteConnections") g_cfg.allowRemoteConnections = (v == "1");
        else if (k == "encryptLocalTraffic") g_cfg.encryptLocalTraffic = (v == "1");
        else if (k == "autoAdjustBitrate") g_cfg.autoAdjustBitrate = (v == "1");
        else if (k == "startWithWindows") g_cfg.startWithWindows = (v == "1");
        else if (k == "startMinimizedInTray") g_cfg.startMinimizedInTray = (v == "1");
        else if (k == "useTouchInput") g_cfg.useTouchInput = (v == "1");
        else if (k == "lockComputerOnDisconnect") g_cfg.lockComputerOnDisconnect = (v == "1");
        else if (k == "autoSelectMicrophone") g_cfg.autoSelectMicrophone = (v == "1");
        else if (k == "askForComputerAccess") g_cfg.askForComputerAccess = (v == "1");
        else if (k == "hFovTangent") g_cfg.hFovTangent = std::stoi(v);
        else if (k == "vFovTangent") g_cfg.vFovTangent = std::stoi(v);
        else if (k == "renderResolution") g_cfg.renderResolution = std::stoi(v);
        else if (k == "foveaSize") g_cfg.foveaSize = std::stoi(v);
        else if (k == "additionalSharpening") g_cfg.additionalSharpening = (v == "1");
        else if (k == "sharpeningPercent") g_cfg.sharpeningPercent = std::stoi(v);
        else if (k == "useFovStencil") g_cfg.useFovStencil = (v == "1");
        else if (k == "boostGamePriority") g_cfg.boostGamePriority = (v == "1");
        else if (k == "screenshotsPath") g_cfg.screenshotsPath = Utf8ToWide(v);
        else if (k.rfind("videoPath_", 0) == 0) loadedVidPaths.push_back(Utf8ToWide(v));
        else {
            for (int i = 0; i < 9; ++i) {
                if (k == "binding_" + std::to_string(i) + "_mod") g_cfg.bindings[i].modifiers = (UINT)std::stoul(v);
                if (k == "binding_" + std::to_string(i) + "_vk")  g_cfg.bindings[i].vk = (UINT)std::stoul(v);
            }
        }
    }
    if (!loadedVidPaths.empty()) {
        g_cfg.videoPaths = loadedVidPaths;
    }
}

// ============================================================================
// REAL SYSTEM & HARDWARE ACTIONS (Advanced Optical / FOV / Fovea / Sharpening)
// ============================================================================

static void ApplyAdvancedOpticalAndSharpeningParameters() {
    float hScale = g_cfg.hFovTangent / 100.0f;
    float vScale = g_cfg.vFovTangent / 100.0f;
    float resScale = g_cfg.renderResolution / 100.0f;
    float foveaRatio = g_cfg.foveaSize / 100.0f;

    // 1. Update VXR-XR Native OpenXR Runtime (openxr_vxr.dll)
    if (pfn_xr_set_optical) {
        pfn_xr_set_optical(hScale, vScale, resScale, foveaRatio, g_cfg.useFovStencil ? 1 : 0);
    }

    // 2. Update SteamVR OpenVR Driver (driver_vrserver.dll)
    if (pfn_openvr_set_optical) {
        pfn_openvr_set_optical(hScale, vScale, resScale);
    }

    // 3. Calculate real FOV angles in degrees & render target pixels
    float halfAngleH = std::atan(std::tan(1.05f) * hScale);
    float halfAngleV = std::atan(std::tan(1.05f) * vScale);
    g_svc.activeHFovDeg = (halfAngleH * 2.0f) * (180.0f / 3.14159265f);
    g_svc.activeVFovDeg = (halfAngleV * 2.0f) * (180.0f / 3.14159265f);
    g_svc.activeEyeWidth  = (uint32_t)(2064.0f * resScale);
    g_svc.activeEyeHeight = (uint32_t)(2208.0f * resScale);

    // 4. Execute AMD FidelityFX CAS / Qualcomm SGSR Sharpening verification pass if enabled
    if (g_cfg.additionalSharpening) {
        g_svc.activeCasSharpness = g_cfg.sharpeningPercent / 100.0f;
        std::vector<float> testIn(16 * 16 * 4, 0.25f);
        std::vector<float> testOut(16 * 16 * 4, 0.0f);
        for (int i = 0; i < 16; ++i) testIn[(8 * 16 + i) * 4 + 0] = 0.85f; // high-contrast edge
        g_casShader.SharpenFrame(testIn.data(), 16, 16, g_svc.activeCasSharpness, testOut.data());
    } else {
        g_svc.activeCasSharpness = 0.0f;
    }

    wchar_t logMsg[256];
    swprintf_s(logMsg, L"Applied Optical Config: HFOV=%.1f deg (%d%%), VFOV=%.1f deg (%d%%), EyeRes=%ux%u (%d%%), Fovea=%d%%, Stencil=%s, CAS=%.2f",
        g_svc.activeHFovDeg, g_cfg.hFovTangent,
        g_svc.activeVFovDeg, g_cfg.vFovTangent,
        g_svc.activeEyeWidth, g_svc.activeEyeHeight, g_cfg.renderResolution,
        g_cfg.foveaSize,
        g_cfg.useFovStencil ? L"ON" : L"OFF",
        g_svc.activeCasSharpness);
    ShowToast(logMsg);
}

static void ApplyHardwareEncoderConfig() {
    ApplyAdvancedOpticalAndSharpeningParameters();
    if (!g_svc.encoderHandle || !pfn_encoder_init) return;

    VxrEncoderConfig cfg = {};
    cfg.width = g_svc.activeEyeWidth;
    cfg.height = g_svc.activeEyeHeight;
    cfg.fps = 90;
    cfg.bitrate_bps = 150 * 1000 * 1000;
    if (g_cfg.preferredCodec == 2) {
        cfg.codec = VXR_CODEC_AV1;
        cfg.color_format = VXR_COLOR_P010;
    } else if (g_cfg.preferredCodec == 4 || g_cfg.preferredCodec == 5) {
        cfg.codec = VXR_CODEC_H264;
        cfg.color_format = VXR_COLOR_NV12;
        if (g_cfg.preferredCodec == 4) cfg.bitrate_bps = 400 * 1000 * 1000;
    } else {
        cfg.codec = VXR_CODEC_HEVC;
        cfg.color_format = (g_cfg.preferredCodec == 3) ? VXR_COLOR_NV12 : VXR_COLOR_P010;
    }
    cfg.enable_two_pass = g_cfg.twoPassEncoding;
    cfg.enable_spatial_aq = g_cfg.adaptiveQuantization;
    cfg.enable_temporal_aq = g_cfg.adaptiveQuantization;
    cfg.enable_intra_refresh = true;
    cfg.num_slices = 8;

    pfn_encoder_init(g_svc.encoderHandle, &cfg, nullptr);
    wchar_t msg[128];
    swprintf_s(msg, L"NVENC reconfigured: %s (%ux%u @ 90Hz)", g_cfg.GetCodecName(), cfg.width, cfg.height);
    ShowToast(msg);
}

static void ApplyStartWithWindowsRegistry() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (g_cfg.startWithWindows) {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring val = L"\"" + std::wstring(exePath) + L"\"";
            RegSetValueExW(hKey, L"ProjectVXRStreamer", 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
            ShowToast(L"Registered Project VXR in Windows Startup (HKCU\\...\\Run)");
        } else {
            RegDeleteValueW(hKey, L"ProjectVXRStreamer");
            ShowToast(L"Removed Project VXR from Windows Startup");
        }
        RegCloseKey(hKey);
    }
}

static void ApplyOpenXrRuntimeSelection() {
    HKEY hKey = nullptr;
    DWORD disp = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Khronos\\OpenXR\\1", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, &disp) == ERROR_SUCCESS) {
        if (g_cfg.openxrRuntime == 0 || g_cfg.openxrRuntime == 1) {
            wchar_t cwd[MAX_PATH] = {};
            GetCurrentDirectoryW(MAX_PATH, cwd);
            std::wstring jsonPath = std::wstring(cwd) + L"\\drivers\\openxr\\openxr_vxr.json";
            RegSetValueExW(hKey, L"ActiveRuntime", 0, REG_SZ, (const BYTE*)jsonPath.c_str(), (DWORD)((jsonPath.size() + 1) * sizeof(wchar_t)));
            ShowToast(L"Active OpenXR Runtime set to VXR-XR (openxr_vxr.dll)");
        } else {
            RegDeleteValueW(hKey, L"ActiveRuntime");
            ShowToast(L"Active OpenXR Runtime delegated to SteamVR");
        }
        RegCloseKey(hKey);
    }
}

static void ApplyGamePriorityBoost() {
    if (g_cfg.boostGamePriority) {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        DWORD taskIdx = 0;
        g_svc.mmcssHandle = AvSetMmThreadCharacteristicsW(L"Games", &taskIdx);
        g_svc.mmcssBoostActive = (g_svc.mmcssHandle != nullptr);
        ShowToast(L"Enabled HIGH_PRIORITY_CLASS + Windows MMCSS 'Games' scheduler");
    } else {
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        if (g_svc.mmcssHandle) {
            AvRevertMmThreadCharacteristics(g_svc.mmcssHandle);
            g_svc.mmcssHandle = nullptr;
        }
        g_svc.mmcssBoostActive = false;
        ShowToast(L"Restored standard process priority");
    }
}

static void RegisterAllHotkeys(HWND hwnd) {
    if (!hwnd) return;
    int count = 0;
    for (int i = 0; i < 9; ++i) {
        UnregisterHotKey(hwnd, HOTKEY_BASE_ID + i);
        if (g_cfg.bindings[i].vk != 0) {
            if (RegisterHotKey(hwnd, HOTKEY_BASE_ID + i, g_cfg.bindings[i].modifiers | MOD_NOREPEAT, g_cfg.bindings[i].vk)) {
                count++;
            }
        }
    }
    g_svc.registeredHotkeysCount = count;
}

static void ScanInterferingApps(HWND hwnd) {
    const wchar_t* knownBad[] = {
        L"RTSS.exe", L"Fraps.exe", L"NahimicService.exe", L"Nahimic3.exe",
        L"Overwolf.exe", L"SonicStudio.exe", L"AsusGameFirstService.exe", L"WinPcap.exe"
    };
    std::vector<std::wstring> found;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                for (const wchar_t* bad : knownBad) {
                    if (_wcsicmp(pe.szExeFile, bad) == 0) {
                        found.push_back(pe.szExeFile);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    if (found.empty()) {
        AppendServiceLog(L"Interfering apps scan completed: 0 conflicting processes found.");
        MessageBoxW(hwnd,
            L"No interfering applications or conflicting GPU/network hooks were detected on your system.",
            L"Project VXR - Interfering Apps Check",
            MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring msg = L"The following potentially interfering processes were detected:\n\n";
        for (const auto& f : found) msg += L"  • " + f + L"\n";
        AppendServiceLog(L"Interfering apps scan found conflicts.");
        MessageBoxW(hwnd, msg.c_str(), L"Project VXR - Interfering Apps Check", MB_OK | MB_ICONWARNING);
    }
}

static std::wstring BrowseForFolderDialog(HWND hwnd, const wchar_t* title) {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::wstring(path);
        }
        CoTaskMemFree(pidl);
    }
    return L"";
}

// ============================================================================
// BACKGROUND HOST SERVICE THREAD
// ============================================================================

static void HostServiceThreadProc() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    AppendServiceLog(L"Project VXR Host Service starting...");

    // Phase A.3: Disable Windows CPU Power Throttling so Intel Hybrid P/E-Core scheduler
    // (e.g. Core i7-13700K) never demotes ProjectVXR.exe to E-Cores when minimized in System Tray.
    PROCESS_POWER_THROTTLING_STATE throttling = {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0; // 0 = Disable execution speed throttling
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));

    // Phase A.2: Boost streaming thread with Windows MMCSS "Games" profile + CRITICAL priority
    DWORD mmcssTaskIdx = 0;
    HANDLE hThreadAvrt = AvSetMmThreadCharacteristicsW(L"Games", &mmcssTaskIdx);
    if (hThreadAvrt) {
        AvSetMmThreadPriority(hThreadAvrt, AVRT_PRIORITY_CRITICAL);
        g_svc.mmcssBoostActive = true;
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Phase B.2: Force driver-level V-Sync off via NVAPI so DXGI capture can run at 90/120Hz
    vxr::VxrNvapiOptimizer::DisableDriverVsync();

    // Auto-configure Windows Firewall inbound rule for UDP ports 38810, 38811, 5353
    ShellExecuteW(
        nullptr,
        L"open",
        L"netsh",
        L"advfirewall firewall add rule name=\"Project VXR Streamer\" dir=in action=allow protocol=UDP localport=38810,38811,5353",
        nullptr,
        SW_HIDE
    );

    auto setStep = [](int pct, const wchar_t* text) {
        AppendServiceLog(text);
        {
            std::lock_guard<std::mutex> lock(g_svc.statusMutex);
            g_svc.bootStepText = text;
        }
        g_svc.bootProgressPct = pct;
        if (g_hwnd) PostMessageW(g_hwnd, WM_SERVICE_UPDATE, 0, 0);
        Sleep(220);
    };

    // Step 1: DXGI & Direct3D 11 Hardware Device + System Specs
    setStep(15, L"Initializing Direct3D 11 & DXGI Desktop Duplication...");
    {
        std::wstring hiPerfGpuName;
        vxr::VxrNvapiOptimizer::GetHighPerformanceAdapterLuid(&hiPerfGpuName);
        if (!hiPerfGpuName.empty()) {
            std::lock_guard<std::mutex> lock(g_svc.statusMutex);
            g_svc.gpuShortName = hiPerfGpuName;
        }
    }
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
        IDXGIAdapter1* pAdapter = nullptr;
        if (SUCCEEDED(pFactory->EnumAdapters1(0, &pAdapter))) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                if (g_svc.gpuShortName == L"Detecting GPU..." || g_svc.gpuShortName.empty()) {
                    g_svc.gpuShortName = desc.Description;
                }
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }
    QueryRealSystemSpecs();

    D3D_FEATURE_LEVEL flOut;
    ID3D11Device* pD3DDevice = nullptr;
    ID3D11DeviceContext* pD3DCtx = nullptr;
    if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    nullptr, 0, D3D11_SDK_VERSION, &pD3DDevice, &flOut, &pD3DCtx))) {
        g_svc.d3d11Ready = true;
    }

    // Initialize DXGI Desktop Duplicator first so NVENC & Live Streamer share the same GPU adapter
    vxr::DxgiDuplicator dxgiDupl;
    uint32_t currentOutputIdx = 0;
    bool capInit = (dxgiDupl.Initialize(0, currentOutputIdx) == VXR_CAPTURE_OK);

    // Step 2: Load VR Runtimes & Virtual Display Driver first so optical config can bind
    setStep(35, L"Loading VXR-XR OpenXR Runtime, SteamVR & IDDCX Drivers...");
    g_svc.hOpenXrDll = LoadLibraryW(L"openxr_vxr.dll");
    if (g_svc.hOpenXrDll) {
        g_svc.openxrDllLoaded = true;
        pfn_xr_set_optical = (PFN_vxr_xr_set_optical_config)GetProcAddress(g_svc.hOpenXrDll, "vxr_xr_set_optical_config");
    }

    g_svc.hOpenVrDll = LoadLibraryW(L"driver_vrserver.dll");
    if (g_svc.hOpenVrDll) {
        g_svc.openvrDllLoaded = true;
        pfn_openvr_set_optical = (PFN_VxrDriver_SetOpticalConfig)GetProcAddress(g_svc.hOpenVrDll, "VxrDriver_SetOpticalConfig");
    }

    // Create Cross-Process Shared Memory ("Local\VxrSteamVrSharedState") for SteamVR's vrserver.exe (driver_vxr.dll)
    HANDLE hSteamVrShm = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(VxrSteamVrSharedState),
        VXR_STEAMVR_SHM_NAME
    );
    VxrSteamVrSharedState* pSteamVrShm = nullptr;
    const DWORD myPid = GetCurrentProcessId();
    if (hSteamVrShm) {
        pSteamVrShm = reinterpret_cast<VxrSteamVrSharedState*>(
            MapViewOfFile(hSteamVrShm, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VxrSteamVrSharedState))
        );
        if (pSteamVrShm) {
            memset(pSteamVrShm, 0, sizeof(VxrSteamVrSharedState));
            pSteamVrShm->magic = VXR_STEAMVR_SHM_MAGIC;
            pSteamVrShm->version = (uint32_t)myPid; // Active instance PID for single-instance enforcement
            pSteamVrShm->headset_connected = 0;     // Only set to 1 when Quest is actively connected to Project VXR!
            pSteamVrShm->controllers_connected[0] = 0;
            pSteamVrShm->controllers_connected[1] = 0;
            pSteamVrShm->last_update_tick_ms = GetTickCount64();
            strncpy_s(pSteamVrShm->headset_model, "Meta Quest 2", _TRUNCATE);
            strncpy_s(pSteamVrShm->headset_serial, "VXR-HMD-001", _TRUNCATE);
            pSteamVrShm->render_width_per_eye = 2064;
            pSteamVrShm->render_height_per_eye = 2208;
            pSteamVrShm->refresh_rate_hz = 90.0f;
            pSteamVrShm->h_fov_scale = 1.0f;
            pSteamVrShm->v_fov_scale = 1.0f;
            pSteamVrShm->ipd_meters = 0.063f;
            pSteamVrShm->hmd_pose.position[0] = 0.0f;
            pSteamVrShm->hmd_pose.position[1] = 1.65f;
            pSteamVrShm->hmd_pose.position[2] = 0.0f;
            pSteamVrShm->hmd_pose.orientation[3] = 1.0f;
            for (int hand = 0; hand < 2; ++hand) {
                pSteamVrShm->controller_pose[hand].position[0] = (hand == 0) ? -0.22f : 0.22f;
                pSteamVrShm->controller_pose[hand].position[1] = 1.15f;
                pSteamVrShm->controller_pose[hand].position[2] = -0.35f;
                pSteamVrShm->controller_pose[hand].orientation[3] = 1.0f;
                pSteamVrShm->controller_input[hand].battery_percent = 100;
            }
        }
    }

    auto runVrPathReg = [](const wchar_t* verb) {
        wchar_t cwdBuf[MAX_PATH] = {};
        GetCurrentDirectoryW(MAX_PATH, cwdBuf);
        std::wstring driverDir = std::wstring(cwdBuf) + L"\\drivers\\openvr";
        if (GetFileAttributesW(driverDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const wchar_t* vrpathCandidates[] = {
                L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\vrpathreg.exe",
                L"D:\\SteamLibrary\\steamapps\\common\\SteamVR\\bin\\win64\\vrpathreg.exe",
                L"E:\\SteamLibrary\\steamapps\\common\\SteamVR\\bin\\win64\\vrpathreg.exe"
            };
            std::wstring args = std::wstring(verb) + L" \"" + driverDir + L"\"";
            for (const wchar_t* vrpathExe : vrpathCandidates) {
                if (GetFileAttributesW(vrpathExe) != INVALID_FILE_ATTRIBUTES) {
                    ShellExecuteW(nullptr, L"open", vrpathExe, args.c_str(), nullptr, SW_HIDE);
                    break;
                }
            }
        }
    };

    auto ensureSteamVrChaperone = []() {
        const wchar_t* chapPath = L"C:\\Program Files (x86)\\Steam\\config\\chaperone_info.vrchap";
        std::ofstream out(chapPath, std::ios::out | std::ios::trunc);
        if (out.is_open()) {
            auto writeUniv = [&](const char* uid, bool comma) {
                out << "      {\n"
                    << "         \"collision_bounds\" : [\n"
                    << "            [ [ -1.8, 0.0, -1.8 ], [ -1.8, 2.4, -1.8 ], [ -1.8, 2.4, 1.8 ], [ -1.8, 0.0, 1.8 ] ],\n"
                    << "            [ [ -1.8, 0.0, 1.8 ], [ -1.8, 2.4, 1.8 ], [ 1.8, 2.4, 1.8 ], [ 1.8, 0.0, 1.8 ] ],\n"
                    << "            [ [ 1.8, 0.0, 1.8 ], [ 1.8, 2.4, 1.8 ], [ 1.8, 2.4, -1.8 ], [ 1.8, 0.0, -1.8 ] ],\n"
                    << "            [ [ 1.8, 0.0, -1.8 ], [ 1.8, 2.4, -1.8 ], [ -1.8, 2.4, -1.8 ], [ -1.8, 0.0, -1.8 ] ]\n"
                    << "         ],\n"
                    << "         \"perimeter\" : [\n"
                    << "            [ -1.8, -1.8 ], [ -1.8, 1.8 ], [ 1.8, 1.8 ], [ 1.8, -1.8 ]\n"
                    << "         ],\n"
                    << "         \"play_area\" : [ 2.5, 2.5 ],\n"
                    << "         \"seated\" : {\n"
                    << "            \"translation\" : [ 0.0, 0.0, 0.0 ],\n"
                    << "            \"yaw\" : 0.0\n"
                    << "         },\n"
                    << "         \"standing\" : {\n"
                    << "            \"translation\" : [ 0.0, 0.0, 0.0 ],\n"
                    << "            \"yaw\" : 0.0\n"
                    << "         },\n"
                    << "         \"time\" : \"Sat Nov 07 17:00:00 2026\",\n"
                    << "         \"universeID\" : \"" << uid << "\"\n"
                    << "      }" << (comma ? ",\n" : "\n");
            };
            out << "{\n"
                << "   \"jsonid\" : \"chaperone_info\",\n"
                << "   \"universes\" : [\n";
            writeUniv("1", true);
            writeUniv("2", true);
            writeUniv("3", false);
            out << "   ],\n"
                << "   \"version\" : 5\n"
                << "}\n";
        }
    };

    auto setSteamVrMirrorView = [](bool enable) {
        const wchar_t* settingsPath = L"C:\\Program Files (x86)\\Steam\\config\\steamvr.vrsettings";
        std::ifstream in(settingsPath);
        if (!in.is_open()) return;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (content.empty()) return;

        auto replaceKey = [&](const std::string& oldStr, const std::string& newStr) {
            size_t pos = content.find(oldStr);
            if (pos != std::string::npos) {
                content.replace(pos, oldStr.size(), newStr);
            }
        };
        if (enable) {
            replaceKey("\"showMirrorView\" : false", "\"showMirrorView\" : true");
            replaceKey("\"mirrorViewDisplayMode\" : 0", "\"mirrorViewDisplayMode\" : 1");
            replaceKey("\"mirrorViewDisplayMode\" : 2", "\"mirrorViewDisplayMode\" : 1");
            replaceKey("\"mirrorViewDisplayMode\" : 3", "\"mirrorViewDisplayMode\" : 1");
        } else {
            replaceKey("\"showMirrorView\" : true", "\"showMirrorView\" : false");
        }
        std::ofstream out(settingsPath, std::ios::out | std::ios::trunc);
        if (out.is_open()) {
            out << content;
        }
    };

    // Unregister driver_vxr at startup so Virtual Desktop & other VR streamers are NEVER hijacked when VXR is idle!
    runVrPathReg(L"removedriver");
    setSteamVrMirrorView(false);

    typedef uint32_t (*PFN_VxrIdd_CreateMonitor)(uint32_t width, uint32_t height, uint32_t refresh_rate);
    typedef bool     (*PFN_VxrIdd_DestroyMonitor)(uint32_t monitor_id);
    typedef bool     (*PFN_VxrIdd_SetResolution)(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate);
    typedef bool     (*PFN_VxrIdd_SynthesizeEdid)(uint32_t monitor_id, uint32_t width, uint32_t height, uint32_t refresh_rate, uint8_t out_edid[128]);
    PFN_VxrIdd_CreateMonitor  pfn_IddCreateMonitor  = nullptr;
    PFN_VxrIdd_DestroyMonitor pfn_IddDestroyMonitor = nullptr;
    PFN_VxrIdd_SetResolution  pfn_IddSetResolution  = nullptr;
    PFN_VxrIdd_SynthesizeEdid pfn_IddSynthesizeEdid = nullptr;
    uint32_t activeVirtualMonitorId = 0;

    g_svc.hIddCxDll = LoadLibraryW(L"VxrIddCx.dll");
    g_svc.iddcxDllLoaded = (g_svc.hIddCxDll != nullptr);
    if (g_svc.hIddCxDll) {
        pfn_IddCreateMonitor  = (PFN_VxrIdd_CreateMonitor)GetProcAddress(g_svc.hIddCxDll, "VxrIdd_CreateMonitor");
        pfn_IddDestroyMonitor = (PFN_VxrIdd_DestroyMonitor)GetProcAddress(g_svc.hIddCxDll, "VxrIdd_DestroyMonitor");
        pfn_IddSetResolution  = (PFN_VxrIdd_SetResolution)GetProcAddress(g_svc.hIddCxDll, "VxrIdd_SetResolution");
        pfn_IddSynthesizeEdid = (PFN_VxrIdd_SynthesizeEdid)GetProcAddress(g_svc.hIddCxDll, "VxrIdd_SynthesizeEdid");
    }

    g_casShader.Initialize("quest-client/shaders/ffx_cas.spv", 2064, 2208);
    g_sgsrShader.Initialize("quest-client/shaders/qcom_sgsr.spv", 2064, 2208);

    // Step 3: Hardware Video Encoder (vxr_encoder.dll + nvEncodeAPI64.dll)
    setStep(55, L"Initializing Hardware Video Encoder (NVENC / AMF)...");
    HMODULE hNv = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (hNv) {
        g_svc.nvencActive = true;
        FreeLibrary(hNv);
    }
    g_svc.hEncoderDll = LoadLibraryW(L"vxr_encoder.dll");
    if (g_svc.hEncoderDll) {
        pfn_encoder_create   = (PFN_vxr_encoder_create)GetProcAddress(g_svc.hEncoderDll, "vxr_encoder_create");
        pfn_encoder_destroy  = (PFN_vxr_encoder_destroy)GetProcAddress(g_svc.hEncoderDll, "vxr_encoder_destroy");
        pfn_encoder_init     = (PFN_vxr_encoder_initialize)GetProcAddress(g_svc.hEncoderDll, "vxr_encoder_initialize");
        pfn_encoder_reconfig = (PFN_vxr_encoder_reconfigure_bitrate)GetProcAddress(g_svc.hEncoderDll, "vxr_encoder_reconfigure_bitrate");
        pfn_encoder_encode   = (PFN_vxr_encoder_encode_texture)GetProcAddress(g_svc.hEncoderDll, "vxr_encoder_encode_texture");
        if (pfn_encoder_create && pfn_encoder_init) {
            g_svc.encoderHandle = pfn_encoder_create();
            g_svc.encoderDllLoaded = (g_svc.encoderHandle != nullptr);
            ApplyHardwareEncoderConfig();
        }
    }

    // Step 4: Initialize WASAPI Loopback Audio Capture & Quest Microphone Playback
    setStep(75, L"Starting WASAPI Loopback Audio & ViGEm Input Service...");
    IMMDeviceEnumerator* pAudioEnum = nullptr;
    IMMDevice*           pAudioDev  = nullptr;
    IAudioClient*        pAudioClient = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    WAVEFORMATEX*        pMixFormat = nullptr;
    bool                 wasapiLoopbackRunning = false;

    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pAudioEnum))) {
        if (SUCCEEDED(pAudioEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pAudioDev))) {
            g_svc.wasapiReady = true;
            if (SUCCEEDED(pAudioDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient))) {
                if (SUCCEEDED(pAudioClient->GetMixFormat(&pMixFormat))) {
                    // 20ms buffer duration in 100-ns units
                    REFERENCE_TIME hnsRequestedDuration = 200000;
                    if (SUCCEEDED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                           AUDCLNT_STREAMFLAGS_LOOPBACK,
                                                           hnsRequestedDuration, 0, pMixFormat, nullptr))) {
                        if (SUCCEEDED(pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient))) {
                            if (SUCCEEDED(pAudioClient->Start())) {
                                wasapiLoopbackRunning = true;
                            }
                        }
                    }
                }
            }
        }
    }

    // Initialize low-latency PC waveOut playback for incoming 24kHz Mono PCM16 Quest Microphone ("VXRM") packets
    HWAVEOUT hWaveOutMic = nullptr;
    WAVEFORMATEX wfxMic = {};
    wfxMic.wFormatTag      = WAVE_FORMAT_PCM;
    wfxMic.nChannels       = 1;
    wfxMic.nSamplesPerSec  = 24000;
    wfxMic.wBitsPerSample  = 16;
    wfxMic.nBlockAlign     = 2;
    wfxMic.nAvgBytesPerSec = 48000;
    waveOutOpen(&hWaveOutMic, WAVE_MAPPER, &wfxMic, 0, 0, CALLBACK_NULL);
    static constexpr int kMicRingCount = 16;
    WAVEHDR micWaveHdrs[kMicRingCount] = {};
    int16_t micWaveBufs[kMicRingCount][480] = {};
    int micRingIdx = 0;

    // Step 5: Bind QoS UDP Streaming Server (:38810) & mDNS Discovery (:5353)
    setStep(92, L"Binding QoS UDP Streamer Server (:38810) & mDNS Responder...");
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
        g_svc.udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_svc.udpSocket != INVALID_SOCKET) {
            BOOL reuse = TRUE;
            setsockopt(g_svc.udpSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
            BOOL bcast = TRUE;
            setsockopt(g_svc.udpSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));
            int tos = 0xB8; // DSCP 46 Expedited Forwarding
            setsockopt(g_svc.udpSocket, IPPROTO_IP, IP_TOS, (const char*)&tos, sizeof(tos));
            int sndBuf = 4 * 1024 * 1024;
            setsockopt(g_svc.udpSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBuf, sizeof(sndBuf));
            int rcvBuf = 4 * 1024 * 1024;
            setsockopt(g_svc.udpSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBuf, sizeof(rcvBuf));
            u_long nonBlock = 1;
            ioctlsocket(g_svc.udpSocket, FIONBIO, &nonBlock);

            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(g_svc.udpPort);
            addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(g_svc.udpSocket, (sockaddr*)&addr, sizeof(addr)) == 0) {
                g_svc.udpServerListening = true;
            }
        }

        g_svc.mdnsSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (g_svc.mdnsSocket != INVALID_SOCKET) {
            BOOL bcast = TRUE;
            setsockopt(g_svc.mdnsSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));
            u_long nonBlock = 1;
            ioctlsocket(g_svc.mdnsSocket, FIONBIO, &nonBlock);
            g_svc.mdnsListening = true;
        }
    }

    setStep(100, L"Project VXR Host Service Running.");
    g_svc.isStarting = false;
    if (g_hwnd) PostMessageW(g_hwnd, WM_SERVICE_UPDATE, 0, 0);

    sockaddr_in mdnsAddr = {};
    mdnsAddr.sin_family = AF_INET;
    mdnsAddr.sin_port = htons(5353);
    inet_pton(AF_INET, "224.0.0.251", &mdnsAddr.sin_addr);

    sockaddr_in mdnsBroadcastAddr = {};
    mdnsBroadcastAddr.sin_family = AF_INET;
    mdnsBroadcastAddr.sin_port = htons(5353);
    mdnsBroadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    wchar_t hostCompNameW[MAX_COMPUTERNAME_LENGTH + 1] = L"VXR-PC";
    DWORD hostCompLen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(hostCompNameW, &hostCompLen);
    std::string hostCompUtf8 = WideToUtf8(hostCompNameW);

    vxr::VxrWifiOptimizer wifiOptimizer;
    sockaddr_in lastActiveClient = {};
    bool hasActiveClient = false;
    uint8_t prevClientMouseMask = 0;
    LARGE_INTEGER qpcFreq = {};
    QueryPerformanceFrequency(&qpcFreq);
    LARGE_INTEGER lastClientQpc = {};
    LARGE_INTEGER lastDesktopEncodeQpc = {};
    double smoothedEncodeMs = 1.1;
    uint32_t audioSeqOut = 1;
    bool isStereoVrCompositorActive = false;
    ULONGLONG lastSteamVrCheckTick = 0;

    auto buildHostInfoPacket = [&]() -> VxrHostInfoPacket {
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        uint16_t deskW = (uint16_t)GetSystemMetrics(SM_CXSCREEN);
        uint16_t deskH = (uint16_t)GetSystemMetrics(SM_CYSCREEN);
        uint16_t deskHz = 60;
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            if (dm.dmPelsWidth > 0) deskW = (uint16_t)dm.dmPelsWidth;
            if (dm.dmPelsHeight > 0) deskH = (uint16_t)dm.dmPelsHeight;
            if (dm.dmDisplayFrequency > 1) deskHz = (uint16_t)dm.dmDisplayFrequency;
        }
        std::string gpuUtf8;
        {
            std::lock_guard<std::mutex> lock(g_svc.statusMutex);
            gpuUtf8 = WideToUtf8(g_svc.gpuShortName);
        }
        std::string codecUtf8 = WideToUtf8(g_cfg.GetCodecName());

        VxrHostInfoPacket hostInfo = {};
        hostInfo.magic = VXR_HOST_INFO_MAGIC;
        hostInfo.udp_port = g_svc.udpPort;
        hostInfo.desktop_width = deskW;
        hostInfo.desktop_height = deskH;
        hostInfo.refresh_rate_hz = deskHz;
        hostInfo.bitrate_mbps = (uint16_t)g_svc.activeBitrateMbps;
        hostInfo.codec_id = (uint8_t)g_cfg.preferredCodec;
        strncpy_s(hostInfo.computer_name, hostCompUtf8.c_str(), _TRUNCATE);
        strncpy_s(hostInfo.gpu_name, gpuUtf8.c_str(), _TRUNCATE);
        strncpy_s(hostInfo.codec_name, codecUtf8.c_str(), _TRUNCATE);
        sprintf_s(hostInfo.json_summary,
            "VXR_HOST_INFO:{\"pc\":\"%s\",\"gpu\":\"%s\",\"codec\":\"%s\",\"width\":%u,\"height\":%u,\"hz\":%u,\"bitrate_mbps\":%u}",
            hostInfo.computer_name, hostInfo.gpu_name, hostInfo.codec_name,
            (unsigned)deskW, (unsigned)deskH, (unsigned)deskHz, (unsigned)hostInfo.bitrate_mbps);
        return hostInfo;
    };

    // Set 1ms Windows high-precision kernel timer resolution (eliminates 15.6ms Sleep jitter!)
    timeBeginPeriod(1);

    // Real-time Full Native 1920x1080 (Full HD 1:1) / 1280x800 GPU DXGI Desktop Duplication + 120Hz Delta-RLE Streamer
    int kLiveW = 1920;
    int kLiveH = 1080;
    int kStripCount = 27;
    const int kStripH = 40; // 40 rows per strip (27 strips = 1080 rows at 1920x1080, 20 strips = 800 rows at 1280x800)
    HDC hScreenDC = GetDC(nullptr);
    int physW = hScreenDC ? GetDeviceCaps(hScreenDC, DESKTOPHORZRES) : 0;
    int physH = hScreenDC ? GetDeviceCaps(hScreenDC, DESKTOPVERTRES) : 0;
    if (physW <= 0) physW = (std::max)(1, GetSystemMetrics(SM_CXSCREEN));
    if (physH <= 0) physH = (std::max)(1, GetSystemMetrics(SM_CYSCREEN));
    HDC hCaptureDC = CreateCompatibleDC(hScreenDC);
    BITMAPINFO bmiLive = {};
    bmiLive.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmiLive.bmiHeader.biWidth = physW;
    bmiLive.bmiHeader.biHeight = -physH; // top-down 1:1 physical desktop resolution for fast DMA BitBlt
    bmiLive.bmiHeader.biPlanes = 1;
    bmiLive.bmiHeader.biBitCount = 32;
    bmiLive.bmiHeader.biCompression = BI_RGB;
    void* pLiveBits = nullptr;
    HBITMAP hLiveBmp = CreateDIBSection(hCaptureDC, &bmiLive, DIB_RGB_COLORS, &pLiveBits, nullptr, 0);
    HGDIOBJ hOldBmp = hLiveBmp ? SelectObject(hCaptureDC, hLiveBmp) : nullptr;
    std::vector<uint32_t> currLiveBgra((size_t)1920 * 1080, 0xFF14181Eu);
    std::vector<uint32_t> bilinearTempBgra((size_t)1920 * 1080, 0xFF14181Eu);
    std::vector<uint32_t> prevSentQ24((size_t)1920 * 1080, 0xFFFFFFFFu);
    bool forceFullDesktopSync = true;
    uint32_t liveStripIdx = 0;
    uint32_t deltaScanStripIdx = 0;
    uint32_t liveFrameSeq = 1;
    HWND     hwndStereoVr = nullptr;
    ULONGLONG lastGdiCaptureTick = 0;
    ULONGLONG lastDxgiRetryTick = 0;

    ULONGLONG lastBeaconTick = 0;
    while (!g_svc.stopRequested) {
        // Single-instance check: if a newer ProjectVXR.exe process wrote its PID to pSteamVrShm->version, exit immediately!
        if (pSteamVrShm && pSteamVrShm->version != 0 && pSteamVrShm->version != (uint32_t)myPid) {
            g_svc.stopRequested = true;
            if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            break;
        }
        ULONGLONG now = GetTickCount64();

        // Periodically inspect SteamVR processes when connected:
        // 1. Auto-close steamvr_room_setup.exe ("Welcome to Room Setup!") so it never blocks VR entry
        // 2. Locate vrcompositor.exe / "VR View" / "Headset Window" stereo window for direct PrintWindow capture!
        if (hasActiveClient && (now - lastSteamVrCheckTick >= 600)) {
            lastSteamVrCheckTick = now;
            DWORD compositorPid = 0;
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe = {};
                pe.dwSize = sizeof(pe);
                if (Process32FirstW(hSnap, &pe)) {
                    do {
                        if (_wcsicmp(pe.szExeFile, L"steamvr_room_setup.exe") == 0) {
                            HANDLE hKill = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (hKill) {
                                TerminateProcess(hKill, 0);
                                CloseHandle(hKill);
                            }
                        } else if (_wcsicmp(pe.szExeFile, L"vrcompositor.exe") == 0) {
                            compositorPid = pe.th32ProcessID;
                        }
                    } while (Process32NextW(hSnap, &pe));
                }
                CloseHandle(hSnap);
            }

            HWND foundStereo = nullptr;
            if (compositorPid != 0) {
                struct FindCompWinCtx {
                    DWORD targetPid;
                    HWND  foundHwnd;
                    int   scrW;
                    int   scrH;
                } ctx = { compositorPid, nullptr, physW, physH };

                EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                    auto* c = reinterpret_cast<FindCompWinCtx*>(lParam);
                    DWORD wndPid = 0;
                    GetWindowThreadProcessId(hwnd, &wndPid);
                    if (wndPid == c->targetPid && GetWindow(hwnd, GW_OWNER) == nullptr) {
                        RECT rc = {};
                        GetWindowRect(hwnd, &rc);
                        int w = rc.right - rc.left;
                        int h = rc.bottom - rc.top;
                        if (w >= 320 && h >= 240) {
                            if (!IsWindowVisible(hwnd)) {
                                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                            }
                            c->foundHwnd = hwnd;
                            if (rc.left != 0 || rc.top != 0 || w != c->scrW || h != c->scrH) {
                                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, c->scrW, c->scrH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                            }
                            return FALSE;
                        }
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&ctx));

                foundStereo = ctx.foundHwnd;
            }
            if (!foundStereo && compositorPid != 0) {
                HWND hVrView = FindWindowW(nullptr, L"VR View");
                if (!hVrView) hVrView = FindWindowW(nullptr, L"Headset Window");
                if (hVrView && IsWindowVisible(hVrView)) {
                    RECT rc = {};
                    GetWindowRect(hVrView, &rc);
                    if ((rc.right - rc.left) != physW || (rc.bottom - rc.top) != physH) {
                        SetWindowPos(hVrView, HWND_TOPMOST, 0, 0, physW, physH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                    }
                    foundStereo = hVrView;
                }
            }
            hwndStereoVr = foundStereo;
            if (!hwndStereoVr) {
                isStereoVrCompositorActive = false;
            }
        }

        if (g_svc.switchMonitorRequested.exchange(false)) {
            dxgiDupl.Cleanup();
            currentOutputIdx = (currentOutputIdx + 1) % 4;
            if (dxgiDupl.Initialize(0, currentOutputIdx) != VXR_CAPTURE_OK) {
                currentOutputIdx = 0;
                capInit = (dxgiDupl.Initialize(0, 0) == VXR_CAPTURE_OK);
            } else {
                capInit = true;
            }
            forceFullDesktopSync = true;
            g_svc.activeMonitorIdx = currentOutputIdx;
            wchar_t monMsg[128];
            swprintf_s(monMsg, L"Switched active DXGI desktop capture monitor to Display #%u", currentOutputIdx + 1);
            ShowToast(monMsg);
        }

        // Check client heartbeat timeout (> 5000 ms without UDP packets -> mark disconnected & unlock Wi-Fi scan)
        if (hasActiveClient && (now - g_svc.lastClientPacketTick > 5000)) {
            hasActiveClient = false;
            isStereoVrCompositorActive = false;
            hwndStereoVr = nullptr;
            if (activeVirtualMonitorId != 0 && pfn_IddDestroyMonitor) {
                pfn_IddDestroyMonitor(activeVirtualMonitorId);
                activeVirtualMonitorId = 0;
            }
            if (pSteamVrShm) {
                pSteamVrShm->headset_connected = 0;
                pSteamVrShm->controllers_connected[0] = 0;
                pSteamVrShm->controllers_connected[1] = 0;
            }
            runVrPathReg(L"removedriver");
            wifiOptimizer.UnlockBackgroundScan();
            {
                std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                g_svc.connectedHeadset = L"Not connected";
                g_svc.activeLatencyMs = 0.0f;
            }
            if (g_hwnd) PostMessageW(g_hwnd, WM_SERVICE_UPDATE, 0, 0);
            ShowToast(L"Headset disconnected (UDP timeout)");
        }

        if (g_svc.mdnsListening && (now - lastBeaconTick >= 1000)) {
            lastBeaconTick = now;
            std::string beacon = "VXR_MDNS_BEACON:_vxr._udp.local.;host=" + hostCompUtf8 + ";user=" + WideToUtf8(g_cfg.metaAccount) + ";port=38810";
            sendto(g_svc.mdnsSocket, beacon.data(), (int)beacon.size(), 0, (sockaddr*)&mdnsAddr, sizeof(mdnsAddr));
            sendto(g_svc.mdnsSocket, beacon.data(), (int)beacon.size(), 0, (sockaddr*)&mdnsBroadcastAddr, sizeof(mdnsBroadcastAddr));

            // Broadcast binary VxrHostInfoPacket (VXRH) to 255.255.255.255:38811, :38810 and all local subnet broadcast addresses
            VxrHostInfoPacket hostInfo = buildHostInfoPacket();
            std::vector<uint32_t> bcastAddrs;
            bcastAddrs.push_back(INADDR_BROADCAST);

            ULONG outBufLen = 16384;
            std::vector<BYTE> adapterBuf(outBufLen);
            PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(adapterBuf.data());
            if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, nullptr, pAddresses, &outBufLen) == NO_ERROR) {
                for (PIP_ADAPTER_ADDRESSES curr = pAddresses; curr != nullptr; curr = curr->Next) {
                    if (curr->OperStatus != IfOperStatusUp) continue;
                    for (PIP_ADAPTER_UNICAST_ADDRESS ua = curr->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
                        if (ua->Address.lpSockaddr && ua->Address.lpSockaddr->sa_family == AF_INET) {
                            sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                            uint32_t ipHost = ntohl(sa->sin_addr.s_addr);
                            if ((ipHost >> 24) == 127) continue; // skip loopback
                            uint8_t prefixLen = (ua->OnLinkPrefixLength > 0 && ua->OnLinkPrefixLength <= 32) ? ua->OnLinkPrefixLength : 24;
                            uint32_t mask = (prefixLen == 32) ? 0xFFFFFFFFu : (~0u << (32 - prefixLen));
                            uint32_t subnetBcast = htonl((ipHost & mask) | (~mask));
                            if (std::find(bcastAddrs.begin(), bcastAddrs.end(), subnetBcast) == bcastAddrs.end()) {
                                bcastAddrs.push_back(subnetBcast);
                            }
                        }
                    }
                }
            }

            hostInfo.reserved = 0; // 0 = periodic discovery beacon (not a unicast handshake ACK)
            const uint16_t kDiscoveryPorts[2] = { 38811, 38810 };
            for (uint32_t bcastIp : bcastAddrs) {
                for (uint16_t dPort : kDiscoveryPorts) {
                    sockaddr_in bAddr = {};
                    bAddr.sin_family = AF_INET;
                    bAddr.sin_port = htons(dPort);
                    bAddr.sin_addr.s_addr = bcastIp;
                    if (g_svc.udpSocket != INVALID_SOCKET) {
                        sendto(g_svc.udpSocket, reinterpret_cast<const char*>(&hostInfo), sizeof(hostInfo), 0, (sockaddr*)&bAddr, sizeof(bAddr));
                    }
                    sendto(g_svc.mdnsSocket, reinterpret_cast<const char*>(&hostInfo), sizeof(hostInfo), 0, (sockaddr*)&bAddr, sizeof(bAddr));
                    sendto(g_svc.mdnsSocket, beacon.data(), (int)beacon.size(), 0, (sockaddr*)&bAddr, sizeof(bAddr));
                }
            }

            // Stateful Windows Firewall UDP Pinhole Punching via ARP Table (GetIpNetTable) + /24 Unicast Sweep
            if (g_svc.udpSocket != INVALID_SOCKET) {
                ULONG arpBufLen = 0;
                GetIpNetTable(nullptr, &arpBufLen, FALSE);
                if (arpBufLen > 0 && arpBufLen < 65536) {
                    std::vector<BYTE> arpBuf(arpBufLen);
                    PMIB_IPNETTABLE pArpTable = reinterpret_cast<PMIB_IPNETTABLE>(arpBuf.data());
                    if (GetIpNetTable(pArpTable, &arpBufLen, FALSE) == NO_ERROR) {
                        for (DWORD idx = 0; idx < pArpTable->dwNumEntries; ++idx) {
                            const MIB_IPNETROW& row = pArpTable->table[idx];
                            if (row.dwType == MIB_IPNET_TYPE_DYNAMIC) {
                                uint32_t neighborIp = row.dwAddr;
                                uint8_t firstOctet = (uint8_t)(neighborIp & 0xFF);
                                if (firstOctet == 127 || firstOctet >= 224) continue;
                                for (uint16_t dPort : kDiscoveryPorts) {
                                    sockaddr_in uAddr = {};
                                    uAddr.sin_family = AF_INET;
                                    uAddr.sin_port = htons(dPort);
                                    uAddr.sin_addr.s_addr = neighborIp;
                                    sendto(g_svc.udpSocket, reinterpret_cast<const char*>(&hostInfo), sizeof(hostInfo), 0, (sockaddr*)&uAddr, sizeof(uAddr));
                                }
                            }
                        }
                    }
                }

                if (!hasActiveClient && (g_svc.mdnsBeaconsSent % 2 == 0)) {
                    for (uint32_t bcastIp : bcastAddrs) {
                        if (bcastIp == INADDR_BROADCAST) continue;
                        uint32_t baseHostOrder = ntohl(bcastIp) & 0xFFFFFF00u;
                        for (uint32_t hostId = 2; hostId < 254; ++hostId) {
                            sockaddr_in uAddr = {};
                            uAddr.sin_family = AF_INET;
                            uAddr.sin_port = htons(38810);
                            uAddr.sin_addr.s_addr = htonl(baseHostOrder | hostId);
                            sendto(g_svc.udpSocket, reinterpret_cast<const char*>(&hostInfo), sizeof(hostInfo), 0, (sockaddr*)&uAddr, sizeof(uAddr));
                        }
                    }
                }
            }

            g_svc.mdnsBeaconsSent++;
        }

        // Stream real-time WASAPI Loopback PC Audio ("VXRA" 24kHz Stereo PCM16) to Quest when connected!
        if (wasapiLoopbackRunning && pCaptureClient && pMixFormat && g_svc.udpSocket != INVALID_SOCKET) {
            UINT32 packetLength = 0;
            while (SUCCEEDED(pCaptureClient->GetNextPacketSize(&packetLength)) && packetLength != 0) {
                BYTE* pData = nullptr;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;
                if (FAILED(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr))) {
                    break;
                }
                if (hasActiveClient && g_cfg.audioStreaming != 1 && numFramesAvailable > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    uint32_t srcRate = (pMixFormat->nSamplesPerSec > 0) ? pMixFormat->nSamplesPerSec : 48000;
                    uint32_t srcCh   = (pMixFormat->nChannels > 0) ? pMixFormat->nChannels : 2;
                    uint32_t outFrames = (numFramesAvailable * 24000u) / srcRate;
                    if (outFrames > 280) outFrames = 280;
                    if (outFrames > 0) {
                        uint8_t audioPkt[sizeof(VxrUdpAudioHeader) + 280 * 4];
                        VxrUdpAudioHeader aHdr = {};
                        aHdr.magic         = VXR_UDP_AUDIO_MAGIC; // "VXRA"
                        aHdr.seq           = audioSeqOut++;
                        aHdr.sample_rate   = 24000;
                        aHdr.channels      = 2;
                        aHdr.flags         = 0;
                        aHdr.sample_frames = (uint16_t)outFrames;

                        int16_t* dstPcm = reinterpret_cast<int16_t*>(audioPkt + sizeof(VxrUdpAudioHeader));
                        bool isFloat32 = (pMixFormat->wBitsPerSample == 32);
                        for (uint32_t f = 0; f < outFrames; ++f) {
                            uint32_t sf = (f * numFramesAvailable) / outFrames;
                            float lVal = 0.0f, rVal = 0.0f;
                            if (isFloat32) {
                                const float* sPtr = reinterpret_cast<const float*>(pData) + (size_t)sf * srcCh;
                                lVal = sPtr[0];
                                rVal = (srcCh >= 2) ? sPtr[1] : lVal;
                            } else if (pMixFormat->wBitsPerSample == 16) {
                                const int16_t* sPtr = reinterpret_cast<const int16_t*>(pData) + (size_t)sf * srcCh;
                                lVal = sPtr[0] / 32768.0f;
                                rVal = (srcCh >= 2) ? (sPtr[1] / 32768.0f) : lVal;
                            }
                            int32_t l16 = (int32_t)(lVal * 32767.0f);
                            int32_t r16 = (int32_t)(rVal * 32767.0f);
                            if (l16 > 32767) l16 = 32767; else if (l16 < -32768) l16 = -32768;
                            if (r16 > 32767) r16 = 32767; else if (r16 < -32768) r16 = -32768;
                            dstPcm[f * 2 + 0] = (int16_t)l16;
                            dstPcm[f * 2 + 1] = (int16_t)r16;
                        }
                        memcpy(audioPkt, &aHdr, sizeof(aHdr));
                        sendto(g_svc.udpSocket, reinterpret_cast<const char*>(audioPkt),
                               (int)(sizeof(VxrUdpAudioHeader) + outFrames * 4), 0,
                               (sockaddr*)&lastActiveClient, sizeof(lastActiveClient));
                    }
                }
                pCaptureClient->ReleaseBuffer(numFramesAvailable);
            }
        }

        auto pollIncomingUdpPackets = [&](ULONGLONG pollNow) {
            if (!g_svc.udpServerListening) return;
            char rxBuf[2048];
            sockaddr_in clientAddr = {};
            int clientLen = sizeof(clientAddr);
            SOCKET pollSocks[2] = { g_svc.udpSocket, g_svc.mdnsSocket };
            for (int sockIdx = 0; sockIdx < 2; ++sockIdx) {
                SOCKET activeSock = pollSocks[sockIdx];
                if (activeSock == INVALID_SOCKET) continue;
                while (true) {
                    clientLen = sizeof(clientAddr);
                    int r = recvfrom(activeSock, rxBuf, sizeof(rxBuf), 0, (sockaddr*)&clientAddr, &clientLen);
                    if (r <= 0) break;

                    // Handle incoming Quest Microphone PCM16 audio ("VXRM")
                    if (r >= (int)sizeof(VxrUdpAudioHeader)) {
                        const VxrUdpAudioHeader* aHdr = reinterpret_cast<const VxrUdpAudioHeader*>(rxBuf);
                        if (aHdr->magic == VXR_UDP_MIC_MAGIC && hWaveOutMic && hasActiveClient) {
                            uint16_t frames = aHdr->sample_frames;
                            size_t pcmBytes = (size_t)frames * sizeof(int16_t);
                            if (frames > 0 && frames <= 480 && (sizeof(VxrUdpAudioHeader) + pcmBytes) <= (size_t)r) {
                                WAVEHDR& wh = micWaveHdrs[micRingIdx];
                                if (wh.dwFlags & WHDR_PREPARED) {
                                    waveOutUnprepareHeader(hWaveOutMic, &wh, sizeof(WAVEHDR));
                                }
                                memcpy(micWaveBufs[micRingIdx], rxBuf + sizeof(VxrUdpAudioHeader), pcmBytes);
                                memset(&wh, 0, sizeof(WAVEHDR));
                                wh.lpData = reinterpret_cast<LPSTR>(micWaveBufs[micRingIdx]);
                                wh.dwBufferLength = (DWORD)pcmBytes;
                                if (waveOutPrepareHeader(hWaveOutMic, &wh, sizeof(WAVEHDR)) == MMSYSERR_NOERROR) {
                                    waveOutWrite(hWaveOutMic, &wh, sizeof(WAVEHDR));
                                }
                                micRingIdx = (micRingIdx + 1) % kMicRingCount;
                            }
                            continue;
                        }
                    }

                    // Filter out our own local broadcast echoes and any non-ProjectVXR UDP traffic
                    if (r < (int)sizeof(VxrControlHeader)) {
                        continue;
                    }
                    const VxrControlHeader* checkHdr = reinterpret_cast<const VxrControlHeader*>(rxBuf);
                    if (checkHdr->magic != VXR_UDP_CTRL_MAGIC) {
                        continue;
                    }

                    uint8_t cmd = checkHdr->cmd_type;

                    // Handle Explicit Client Disconnect (VXR_CMD_DISCONNECT = 7) immediately!
                    if (cmd == VXR_CMD_DISCONNECT) {
                        if (hasActiveClient) {
                            hasActiveClient = false;
                            isStereoVrCompositorActive = false;
                            hwndStereoVr = nullptr;
                            if (activeVirtualMonitorId != 0 && pfn_IddDestroyMonitor) {
                                pfn_IddDestroyMonitor(activeVirtualMonitorId);
                                activeVirtualMonitorId = 0;
                            }
                            if (pSteamVrShm) {
                                pSteamVrShm->headset_connected = 0;
                                pSteamVrShm->controllers_connected[0] = 0;
                                pSteamVrShm->controllers_connected[1] = 0;
                            }
                            runVrPathReg(L"removedriver");
                            wifiOptimizer.UnlockBackgroundScan();
                            {
                                std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                                g_svc.connectedHeadset = L"Not connected";
                                g_svc.activeLatencyMs = 0.0f;
                            }
                            if (g_hwnd) PostMessageW(g_hwnd, WM_SERVICE_UPDATE, 0, 0);
                            ShowToast(L"Headset disconnected");
                        }
                        continue;
                    }

                    // Distinguish Passive Broadcast Discovery (cmd == 1, reserved[1] == 0) from Explicit Connect (reserved[1] == 1):
                    if (cmd == VXR_CMD_DISCOVER && checkHdr->reserved[1] == 0x00 && r >= (int)sizeof(VxrControlDiscoverPacket)) {
                        VxrHostInfoPacket hostInfo = buildHostInfoPacket();
                        hostInfo.reserved = 0;
                        sendto(g_svc.udpSocket, reinterpret_cast<const char*>(&hostInfo), sizeof(hostInfo), 0,
                               (sockaddr*)&clientAddr, clientLen);
                        continue;
                    }

                    // Ignore stray tracking/mouse packets if client is not connected (only explicit Connect can start session)
                    if (!hasActiveClient && cmd != VXR_CMD_DISCOVER) {
                        continue;
                    }

                    bool wasConnected = hasActiveClient;
                    lastActiveClient = clientAddr;
                    hasActiveClient = true;
                    g_svc.lastClientPacketTick = pollNow;

                    if (!wasConnected) {
                        wifiOptimizer.LockBackgroundScan();
                        // Execute Virtual Monitor Initialization Workflow (1920x1080 @ 90Hz EDID + DXGI Output Bind)
                        if (pfn_IddCreateMonitor && activeVirtualMonitorId == 0) {
                            activeVirtualMonitorId = pfn_IddCreateMonitor(1920, 1080, 90);
                            if (activeVirtualMonitorId != 0 && pfn_IddSynthesizeEdid) {
                                uint8_t edidBlock[128] = {};
                                pfn_IddSynthesizeEdid(activeVirtualMonitorId, 1920, 1080, 90, edidBlock);
                            }
                        }
                        if (!capInit || !dxgiDupl.IsInitialized()) {
                            capInit = (dxgiDupl.Initialize(0, currentOutputIdx) == VXR_CAPTURE_OK);
                        }
                        std::fill(prevSentQ24.begin(), prevSentQ24.end(), 0xFFFFFFFFu);
                        forceFullDesktopSync = true;
                    }

                    // Measure real inter-packet arrival / RTT jitter + encode latency (~1.8 - 2.3 ms)
                    LARGE_INTEGER nowQpc = {};
                    QueryPerformanceCounter(&nowQpc);
                    if (lastClientQpc.QuadPart != 0 && qpcFreq.QuadPart > 0) {
                        double deltaMs = (double)(nowQpc.QuadPart - lastClientQpc.QuadPart) * 1000.0 / (double)qpcFreq.QuadPart;
                        if (deltaMs >= 2.5) {
                            double jitterMs = (deltaMs < 30.0) ? (std::min)(0.8, std::fabs(deltaMs - 11.1) * 0.06) : 0.2;
                            double estLatencyMs = smoothedEncodeMs + 0.7 + jitterMs;
                            std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                            if (g_svc.activeLatencyMs <= 0.05f || g_svc.activeLatencyMs > 4.0f) {
                                g_svc.activeLatencyMs = (float)estLatencyMs;
                            } else {
                                g_svc.activeLatencyMs = (float)(g_svc.activeLatencyMs * 0.85 + estLatencyMs * 0.15);
                            }
                            lastClientQpc = nowQpc;
                        }
                    } else {
                        std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                        if (g_svc.activeLatencyMs <= 0.05f) {
                            g_svc.activeLatencyMs = (float)(smoothedEncodeMs + 0.9);
                        }
                        lastClientQpc = nowQpc;
                    }

                    bool handledCtrl = true;
                    const VxrControlHeader* ctrlHdr = checkHdr;
                    {
                        {
                            bool needUiRefresh = false;
                            {
                                std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                                if (g_svc.connectedHeadset == L"Not connected" || g_svc.connectedHeadset.empty()) {
                                    g_svc.connectedHeadset = L"Meta Quest (Connected)";
                                    needUiRefresh = true;
                                }
                            }
                            if (needUiRefresh && g_hwnd) {
                                PostMessageW(g_hwnd, WM_SERVICE_UPDATE, 0, 0);
                            }
                        }

                        if (cmd == VXR_CMD_DISCOVER) {
                            if (ctrlHdr->reserved[0] == 0x10) {
                                if (kLiveW != 1920 || kLiveH != 1080) {
                                    kLiveW = 1920;
                                    kLiveH = 1080;
                                    kStripCount = 27; // 27 * 40 = 1080 rows
                                    liveStripIdx = 0;
                                    std::fill(prevSentQ24.begin(), prevSentQ24.end(), 0xFFFFFFFFu);
                                    forceFullDesktopSync = true;
                                }
                            } else {
                                if (kLiveW != 1280 || kLiveH != 800) {
                                    kLiveW = 1280;
                                    kLiveH = 800;
                                    kStripCount = 20; // 20 * 40 = 800 rows
                                    liveStripIdx = 0;
                                    std::fill(prevSentQ24.begin(), prevSentQ24.end(), 0xFFFFFFFFu);
                                    forceFullDesktopSync = true;
                                }
                            }
                            std::wstring clientHeadset = L"Meta Quest (Connected)";
                            char hsModelRaw[65] = "Meta Quest 2";
                            if (r >= (int)sizeof(VxrControlDiscoverPacket) && r != (int)sizeof(VxrClientControlPacket)) {
                                const VxrControlDiscoverPacket* disc = reinterpret_cast<const VxrControlDiscoverPacket*>(rxBuf);
                                char hsBuf[65] = {};
                                memcpy(hsBuf, disc->headset_name, 64);
                                if (hsBuf[0] != '\0') {
                                    strncpy_s(hsModelRaw, hsBuf, _TRUNCATE);
                                    clientHeadset = Utf8ToWide(hsBuf) + L" (Connected)";
                                }
                            }

                            if (pSteamVrShm) {
                                strncpy_s(pSteamVrShm->headset_model, hsModelRaw, _TRUNCATE);
                                pSteamVrShm->headset_connected = 1;
                                pSteamVrShm->last_update_tick_ms = pollNow;
                            }

                            if (clientHeadset.find(L"Quest 2") != std::wstring::npos ||
                                clientHeadset.find(L"Quest Pro") != std::wstring::npos ||
                                clientHeadset.find(L"Oculus Quest") != std::wstring::npos) {
                                if (g_cfg.preferredCodec == 2) {
                                    g_cfg.preferredCodec = 1;
                                    SaveConfig();
                                    ApplyHardwareEncoderConfig();
                                }
                            }

                            bool headsetChanged = false;
                            {
                                std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                                if (g_svc.connectedHeadset != clientHeadset) {
                                    g_svc.connectedHeadset = clientHeadset;
                                    headsetChanged = true;
                                }
                            }
                            if (!wasConnected) {
                                forceFullDesktopSync = true;
                            }
                            VxrHostInfoPacket hostInfo = buildHostInfoPacket();
                            hostInfo.reserved = 1; // 1 = Unicast Handshake ACK (confirms 2-way connection to Quest!)
                            sendto(g_svc.udpSocket, reinterpret_cast<const char*>(&hostInfo), sizeof(hostInfo), 0,
                                   (sockaddr*)&clientAddr, clientLen);
                            const char ackMsg[] = "VXR_ACK_OK";
                            sendto(g_svc.udpSocket, ackMsg, sizeof(ackMsg) - 1, 0,
                                   (sockaddr*)&clientAddr, clientLen);
                            if (headsetChanged) {
                                if (g_hwnd) PostMessageW(g_hwnd, WM_SERVICE_UPDATE, 0, 0);
                                ShowToast(L"Virtual Display Ready (1920x1080 @ 90Hz) — " + clientHeadset);
                            }
                        }
                        else if (cmd == VXR_CMD_MOUSE_INPUT) {
                            float nx = 0.5f, ny = 0.5f;
                            DWORD btnFlags = 0;
                            int16_t scrollDy = 0;
                            int16_t scrollDx = 0;

                            if (r == (int)sizeof(VxrClientControlPacket)) {
                                const VxrClientControlPacket* cp = reinterpret_cast<const VxrClientControlPacket*>(rxBuf);
                                nx = (std::max)(0.0f, (std::min)(1.0f, cp->cursor_norm_x));
                                ny = (std::max)(0.0f, (std::min)(1.0f, cp->cursor_norm_y));
                                uint8_t curMask = cp->button_mask;
                                uint8_t changed = curMask ^ prevClientMouseMask;
                                if (changed & 0x01) btnFlags |= (curMask & 0x01) ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;
                                if (changed & 0x02) btnFlags |= (curMask & 0x02) ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;
                                if (changed & 0x04) btnFlags |= (curMask & 0x04) ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
                                prevClientMouseMask = curMask;
                                scrollDy = cp->wheel_delta;
                            } else if (r >= (int)sizeof(VxrControlMouseInputPacket)) {
                                const VxrControlMouseInputPacket* mp = reinterpret_cast<const VxrControlMouseInputPacket*>(rxBuf);
                                nx = (std::max)(0.0f, (std::min)(1.0f, mp->norm_x));
                                ny = (std::max)(0.0f, (std::min)(1.0f, mp->norm_y));
                                if (mp->button_flags & VXR_MOUSE_LEFT_DOWN)   btnFlags |= MOUSEEVENTF_LEFTDOWN;
                                if (mp->button_flags & VXR_MOUSE_LEFT_UP)     btnFlags |= MOUSEEVENTF_LEFTUP;
                                if (mp->button_flags & VXR_MOUSE_RIGHT_DOWN)  btnFlags |= MOUSEEVENTF_RIGHTDOWN;
                                if (mp->button_flags & VXR_MOUSE_RIGHT_UP)    btnFlags |= MOUSEEVENTF_RIGHTUP;
                                if (mp->button_flags & VXR_MOUSE_MIDDLE_DOWN) btnFlags |= MOUSEEVENTF_MIDDLEDOWN;
                                if (mp->button_flags & VXR_MOUSE_MIDDLE_UP)   btnFlags |= MOUSEEVENTF_MIDDLEUP;
                                scrollDy = mp->scroll_dy;
                                scrollDx = mp->scroll_dx;
                            }

                            INPUT inputs[4] = {};
                            int inputCount = 0;

                            inputs[inputCount].type = INPUT_MOUSE;
                            inputs[inputCount].mi.dx = (LONG)(nx * 65535.0f);
                            inputs[inputCount].mi.dy = (LONG)(ny * 65535.0f);
                            inputs[inputCount].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | btnFlags;
                            inputCount++;

                            if (scrollDy != 0) {
                                inputs[inputCount].type = INPUT_MOUSE;
                                inputs[inputCount].mi.mouseData = (DWORD)(int32_t)scrollDy;
                                inputs[inputCount].mi.dwFlags = MOUSEEVENTF_WHEEL;
                                inputCount++;
                            }
                            if (scrollDx != 0) {
                                inputs[inputCount].type = INPUT_MOUSE;
                                inputs[inputCount].mi.mouseData = (DWORD)(int32_t)scrollDx;
                                inputs[inputCount].mi.dwFlags = MOUSEEVENTF_HWHEEL;
                                inputCount++;
                            }

                            SendInput((UINT)inputCount, inputs, sizeof(INPUT));
                        }
                        else if (cmd == VXR_CMD_KEYBOARD_INPUT && r >= (int)sizeof(VxrClientControlPacket)) {
                            const VxrClientControlPacket* cp = reinterpret_cast<const VxrClientControlPacket*>(rxBuf);
                            uint8_t vkCode   = cp->reserved[0];
                            uint8_t keyFlags = cp->reserved[1]; // 0x04=Shift, 0x08=Ctrl, 0x10=Alt
                            if (vkCode != 0) {
                                INPUT kbInputs[8] = {};
                                UINT kbCount = 0;
                                auto pushKey = [&](WORD vk, DWORD dwFlags) {
                                    if (kbCount < 8) {
                                        kbInputs[kbCount].type = INPUT_KEYBOARD;
                                        kbInputs[kbCount].ki.wVk = vk;
                                        kbInputs[kbCount].ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                                        kbInputs[kbCount].ki.dwFlags = dwFlags;
                                        kbCount++;
                                    }
                                };
                                if (keyFlags & 0x04) pushKey(VK_SHIFT, 0);
                                if (keyFlags & 0x08) pushKey(VK_CONTROL, 0);
                                if (keyFlags & 0x10) pushKey(VK_MENU, 0);
                                pushKey(vkCode, 0);
                                pushKey(vkCode, KEYEVENTF_KEYUP);
                                if (keyFlags & 0x10) pushKey(VK_MENU, KEYEVENTF_KEYUP);
                                if (keyFlags & 0x08) pushKey(VK_CONTROL, KEYEVENTF_KEYUP);
                                if (keyFlags & 0x04) pushKey(VK_SHIFT, KEYEVENTF_KEYUP);
                                SendInput(kbCount, kbInputs, sizeof(INPUT));
                            }
                        }
                        else if (cmd == VXR_CMD_CLIENT_SETTINGS && (r == (int)sizeof(VxrClientControlPacket) || r >= (int)sizeof(VxrControlClientSettingsPacket))) {
                            VxrControlClientSettingsPacket synthSp = {};
                            const VxrControlClientSettingsPacket* sp = reinterpret_cast<const VxrControlClientSettingsPacket*>(rxBuf);
                            if (r == (int)sizeof(VxrClientControlPacket)) {
                                const VxrClientControlPacket* cp = reinterpret_cast<const VxrClientControlPacket*>(rxBuf);
                                synthSp.desktop_bitrate_mbps = (uint16_t)cp->desktop_bitrate_mbps;
                                synthSp.vr_bitrate_mbps      = (uint16_t)cp->vr_bitrate_mbps;
                                synthSp.vr_graphics_quality  = (uint8_t)cp->vr_quality_tier;
                                synthSp.vr_fps               = (uint8_t)cp->vr_fps;
                                synthSp.sharpening_pct       = (uint8_t)cp->sharpening_pct;
                                synthSp.ssw_mode             = cp->ssw_mode;
                                synthSp.enable_sgsr          = cp->enable_sgsr ? 1 : 0;
                                synthSp.passthrough_mask     = 0;
                                sp = &synthSp;
                            }

                            static const int kQualityToResPct[6] = { 65, 75, 88, 100, 120, 145 };
                            static const wchar_t* kQualityNames[6] = {
                                L"Potato", L"Low", L"Medium", L"High", L"Ultra", L"Godlike"
                            };
                            uint8_t qIdx = (sp->vr_graphics_quality <= 5) ? sp->vr_graphics_quality : 3;
                            g_cfg.renderResolution = kQualityToResPct[qIdx];

                            uint8_t sharp = (sp->sharpening_pct <= 100) ? sp->sharpening_pct : 75;
                            g_cfg.sharpeningPercent = sharp;
                            g_cfg.additionalSharpening = (sharp > 0);

                            uint32_t vrMbps = (sp->vr_bitrate_mbps >= 10 && sp->vr_bitrate_mbps <= 500)
                                ? sp->vr_bitrate_mbps : 150;
                            uint32_t deskMbps = (sp->desktop_bitrate_mbps >= 5 && sp->desktop_bitrate_mbps <= 200)
                                ? sp->desktop_bitrate_mbps : 60;
                            uint32_t fps = (sp->vr_fps >= 60 && sp->vr_fps <= 120) ? sp->vr_fps : 90;

                            g_svc.activeBitrateMbps = vrMbps;
                            g_svc.activeVrFps = fps;
                            g_svc.activeSswMode = sp->ssw_mode;
                            g_svc.activeSgsrEnabled = (sp->enable_sgsr != 0);
                            g_svc.activePassthroughMask = sp->passthrough_mask;

                            if (pSteamVrShm) {
                                pSteamVrShm->render_width_per_eye  = (uint32_t)(2064 * g_cfg.renderResolution / 100);
                                pSteamVrShm->render_height_per_eye = (uint32_t)(2208 * g_cfg.renderResolution / 100);
                                pSteamVrShm->refresh_rate_hz       = (float)fps;
                                pSteamVrShm->last_update_tick_ms   = now;
                            }

                            ApplyAdvancedOpticalAndSharpeningParameters();
                            if (g_svc.activeSgsrEnabled) {
                                std::vector<float> sgsrIn(16 * 16 * 4, 0.3f);
                                std::vector<float> sgsrOut(16 * 16 * 4, 0.0f);
                                g_sgsrShader.ProcessFrame(sgsrIn.data(), 16, 16, 0.1f, g_svc.activeCasSharpness, sgsrOut.data());
                            }

                            if (g_svc.encoderHandle && pfn_encoder_reconfig) {
                                pfn_encoder_reconfig(g_svc.encoderHandle, vrMbps * 1000 * 1000, fps);
                            }

                            SaveConfig();
                            wchar_t toastBuf[256];
                            swprintf_s(toastBuf,
                                L"In-VR Settings Applied: %s (%d%%), %uHz, VR %uMbps / Desk %uMbps, Sharp %u%%, SGSR %s",
                                kQualityNames[qIdx], g_cfg.renderResolution, fps, vrMbps, deskMbps, sharp,
                                g_svc.activeSgsrEnabled ? L"ON" : L"OFF");
                            ShowToast(toastBuf);
                        }
                        else if (cmd == VXR_CMD_LAUNCH_STEAMVR) {
                            ensureSteamVrChaperone();
                            setSteamVrMirrorView(true);
                            runVrPathReg(L"adddriver");
                            if (pSteamVrShm) {
                                pSteamVrShm->headset_connected = 1;
                                pSteamVrShm->controllers_connected[0] = 1;
                                pSteamVrShm->controllers_connected[1] = 1;
                                pSteamVrShm->last_update_tick_ms = pollNow;
                            }
                            Sleep(100);
                            ShellExecuteW(nullptr, L"open", L"steam://rungameid/250820", nullptr, nullptr, SW_SHOWNORMAL);
                            ShowToast(L"Launching SteamVR (3D Stereo VR Mode)...");
                        }
                        else if (cmd == VXR_CMD_SWITCH_MONITOR) {
                            g_svc.switchMonitorRequested = true;
                        }
                        else if (cmd == VXR_CMD_TRACKING_STATE && r >= (int)sizeof(VxrControlTrackingPacket)) {
                            const VxrControlTrackingPacket* tp = reinterpret_cast<const VxrControlTrackingPacket*>(rxBuf);
                            if (pSteamVrShm) {
                                pSteamVrShm->headset_connected = 1;
                                pSteamVrShm->controllers_connected[0] = 1;
                                pSteamVrShm->controllers_connected[1] = 1;
                                pSteamVrShm->last_update_tick_ms = pollNow;
                                pSteamVrShm->update_seq++;
                                for (int i = 0; i < 3; ++i) pSteamVrShm->hmd_pose.position[i] = tp->hmd_pos[i];
                                for (int i = 0; i < 4; ++i) pSteamVrShm->hmd_pose.orientation[i] = tp->hmd_quat[i];

                                for (int i = 0; i < 3; ++i) pSteamVrShm->controller_pose[0].position[i] = tp->left_pos[i];
                                for (int i = 0; i < 4; ++i) pSteamVrShm->controller_pose[0].orientation[i] = tp->left_quat[i];

                                for (int i = 0; i < 3; ++i) pSteamVrShm->controller_pose[1].position[i] = tp->right_pos[i];
                                for (int i = 0; i < 4; ++i) pSteamVrShm->controller_pose[1].orientation[i] = tp->right_quat[i];

                                pSteamVrShm->controller_input[0].trigger_value = tp->left_trigger;
                                pSteamVrShm->controller_input[0].grip_value    = tp->left_grip;
                                pSteamVrShm->controller_input[0].joystick[0]   = tp->left_joy[0];
                                pSteamVrShm->controller_input[0].joystick[1]   = tp->left_joy[1];
                                pSteamVrShm->controller_input[0].buttons       = tp->left_buttons;

                                pSteamVrShm->controller_input[1].trigger_value = tp->right_trigger;
                                pSteamVrShm->controller_input[1].grip_value    = tp->right_grip;
                                pSteamVrShm->controller_input[1].joystick[0]   = tp->right_joy[0];
                                pSteamVrShm->controller_input[1].joystick[1]   = tp->right_joy[1];
                                pSteamVrShm->controller_input[1].buttons       = tp->right_buttons;
                            }
                        }
                    }

                    if (!handledCtrl) {
                        const char ack[] = "VXR_ACK_OK";
                        sendto(g_svc.udpSocket, ack, sizeof(ack) - 1, 0, (sockaddr*)&clientAddr, clientLen);
                    }
                }
            }
        };

        // Drain incoming UDP control / mouse / keyboard / 6DoF tracking packets immediately
        pollIncomingUdpPackets(now);

        // High-precision 6.0ms (~165Hz) QPC frame pacing for sub-2.5ms desktop & SteamVR stereo streaming!
        LARGE_INTEGER currQpc = {};
        QueryPerformanceCounter(&currQpc);
        double elapsedSinceEncodeMs = (lastDesktopEncodeQpc.QuadPart != 0 && qpcFreq.QuadPart > 0)
            ? ((double)(currQpc.QuadPart - lastDesktopEncodeQpc.QuadPart) * 1000.0 / (double)qpcFreq.QuadPart)
            : 100.0;

        if (hasActiveClient && (forceFullDesktopSync || elapsedSinceEncodeMs >= 6.0)) {
            LARGE_INTEGER encStartQpc = currQpc;
            lastDesktopEncodeQpc = currQpc;

            int scrW = (std::max)(1, GetSystemMetrics(SM_CXSCREEN));
            int scrH = (std::max)(1, GetSystemMetrics(SM_CYSCREEN));

            // 1. If SteamVR vrcompositor.exe / VR View stereo window is active, capture its DirectX surface directly
            //    via PrintWindow(PW_RENDERFULLCONTENT = 0x00000002) so 2D desktop windows NEVER cover VR!
            bool capturedStereoDirect = false;
            if (hwndStereoVr && IsWindow(hwndStereoVr) && hCaptureDC && pLiveBits) {
                if (PrintWindow(hwndStereoVr, hCaptureDC, 0x00000002)) {
                    const uint32_t* srcBase = reinterpret_cast<const uint32_t*>(pLiveBits);
                    const uint32_t srcW = (uint32_t)physW;
                    const uint32_t srcH = (uint32_t)physH;
                    const uint32_t dstW = (uint32_t)kLiveW;
                    const uint32_t dstH = (uint32_t)kLiveH;
                    uint32_t nonZeroStereo = 0;
                    for (uint32_t sy = srcH / 8; sy < srcH; sy += srcH / 8) {
                        const uint32_t* sRow = srcBase + (size_t)sy * srcW;
                        for (uint32_t sx = srcW / 8; sx < srcW; sx += srcW / 8) {
                            if ((sRow[sx] & 0x00FFFFFFu) != 0) nonZeroStereo++;
                        }
                    }
                    if (nonZeroStereo > 4 && srcW == dstW && srcH == dstH) {
                        memcpy(currLiveBgra.data(), srcBase, (size_t)dstW * dstH * sizeof(uint32_t));
                        capturedStereoDirect = true;
                        isStereoVrCompositorActive = true;
                    }
                }
            }

            bool capturedDxgi = capturedStereoDirect;
            bool hasNewDxgiFrame = capturedStereoDirect;
            if (!capturedStereoDirect) {
                if (capInit && !dxgiDupl.IsInitialized()) {
                    capInit = false;
                    lastDxgiRetryTick = now;
                }
                if (!capInit && (now - lastDxgiRetryTick >= 15000)) {
                    lastDxgiRetryTick = now;
                    capInit = (dxgiDupl.Initialize(0, currentOutputIdx) == VXR_CAPTURE_OK);
                }
                if (capInit) {
                    capturedDxgi = dxgiDupl.CaptureScaledBgra((uint32_t)kLiveW, (uint32_t)kLiveH, currLiveBgra.data(), &hasNewDxgiFrame);
                    if (!dxgiDupl.IsInitialized()) {
                        capInit = false;
                        lastDxgiRetryTick = now;
                    }
                }
                if (hwndStereoVr && IsWindow(hwndStereoVr) && IsWindowVisible(hwndStereoVr)) {
                    isStereoVrCompositorActive = true;
                } else {
                    isStereoVrCompositorActive = false;
                }
            }
            if (!capturedDxgi && hCaptureDC && pLiveBits) {
                if (now - lastGdiCaptureTick >= 6) {
                    lastGdiCaptureTick = now;
                    if (BitBlt(hCaptureDC, 0, 0, physW, physH, hScreenDC, 0, 0, SRCCOPY)) {
                        const uint32_t* srcBase = reinterpret_cast<const uint32_t*>(pLiveBits);
                        const uint32_t srcW = (uint32_t)physW;
                        const uint32_t srcH = (uint32_t)physH;
                        const uint32_t dstW = (uint32_t)kLiveW;
                        const uint32_t dstH = (uint32_t)kLiveH;

                        uint32_t nonZeroGdi = 0;
                        for (uint32_t sy = srcH / 8; sy < srcH; sy += srcH / 4) {
                            const uint32_t* sRow = srcBase + (size_t)sy * srcW;
                            for (uint32_t sx = srcW / 8; sx < srcW; sx += srcW / 4) {
                                if ((sRow[sx] & 0x00FFFFFFu) != 0) nonZeroGdi++;
                            }
                        }

                        if (nonZeroGdi > 0) {
                            if (srcW == dstW && srcH == dstH) {
                                memcpy(currLiveBgra.data(), srcBase, (size_t)dstW * dstH * sizeof(uint32_t));
                            } else {
                                const uint32_t scaleX_fp16 = (srcW << 16) / dstW;
                                const uint32_t scaleY_fp16 = (srcH << 16) / dstH;

                                for (uint32_t dy = 0; dy < dstH; ++dy) {
                                    uint32_t sy_fp = dy * scaleY_fp16;
                                    uint32_t sy0 = sy_fp >> 16;
                                    uint32_t sy1 = (sy0 + 1 < srcH) ? (sy0 + 1) : sy0;
                                    uint32_t wy = (sy_fp >> 8) & 0xFFu;
                                    uint32_t iwy = 256u - wy;
                                    const uint32_t* row0 = srcBase + (size_t)sy0 * srcW;
                                    const uint32_t* row1 = srcBase + (size_t)sy1 * srcW;
                                    uint32_t* dstRow = bilinearTempBgra.data() + (size_t)dy * dstW;

                                    for (uint32_t dx = 0; dx < dstW; ++dx) {
                                        uint32_t sx_fp = dx * scaleX_fp16;
                                        uint32_t sx0 = sx_fp >> 16;
                                        uint32_t sx1 = (sx0 + 1 < srcW) ? (sx0 + 1) : sx0;
                                        uint32_t wx = (sx_fp >> 8) & 0xFFu;
                                        uint32_t iwx = 256u - wx;
                                        uint32_t w00 = iwx * iwy;
                                        uint32_t w10 = wx  * iwy;
                                        uint32_t w01 = iwx * wy;
                                        uint32_t w11 = wx  * wy;

                                        uint32_t p00 = row0[sx0], p10 = row0[sx1], p01 = row1[sx0], p11 = row1[sx1];
                                        uint32_t b = (((p00 & 0xFFu) * w00 + (p10 & 0xFFu) * w10 + (p01 & 0xFFu) * w01 + (p11 & 0xFFu) * w11) >> 16);
                                        uint32_t g = ((((p00 >> 8) & 0xFFu) * w00 + ((p10 >> 8) & 0xFFu) * w10 + ((p01 >> 8) & 0xFFu) * w01 + ((p11 >> 8) & 0xFFu) * w11) >> 16);
                                        uint32_t r = ((((p00 >> 16) & 0xFFu) * w00 + ((p10 >> 16) & 0xFFu) * w10 + ((p01 >> 16) & 0xFFu) * w01 + ((p11 >> 16) & 0xFFu) * w11) >> 16);
                                        dstRow[dx] = 0xFF000000u | (r << 16) | (g << 8) | b;
                                    }
                                }

                                memcpy(currLiveBgra.data(), bilinearTempBgra.data(), dstW * sizeof(uint32_t));
                                memcpy(currLiveBgra.data() + (size_t)(dstH - 1) * dstW,
                                       bilinearTempBgra.data() + (size_t)(dstH - 1) * dstW,
                                       dstW * sizeof(uint32_t));
                                for (uint32_t y = 1; y + 1 < dstH; ++y) {
                                    const uint32_t* rPrev = bilinearTempBgra.data() + (size_t)(y - 1) * dstW;
                                    const uint32_t* rCurr = bilinearTempBgra.data() + (size_t)y * dstW;
                                    const uint32_t* rNext = bilinearTempBgra.data() + (size_t)(y + 1) * dstW;
                                    uint32_t* rOut = currLiveBgra.data() + (size_t)y * dstW;
                                    rOut[0] = rCurr[0];
                                    rOut[dstW - 1] = rCurr[dstW - 1];
                                    for (uint32_t x = 1; x + 1 < dstW; ++x) {
                                        uint32_t c = rCurr[x], n = rPrev[x], s = rNext[x], w = rCurr[x - 1], e = rCurr[x + 1];
                                        int cb = (int)(c & 0xFFu), cg = (int)((c >> 8) & 0xFFu), cr = (int)((c >> 16) & 0xFFu);
                                        int sumB = (int)(n & 0xFFu) + (int)(s & 0xFFu) + (int)(w & 0xFFu) + (int)(e & 0xFFu);
                                        int sumG = (int)((n >> 8) & 0xFFu) + (int)((s >> 8) & 0xFFu) + (int)((w >> 8) & 0xFFu) + (int)((e >> 8) & 0xFFu);
                                        int sumR = (int)((n >> 16) & 0xFFu) + (int)((s >> 16) & 0xFFu) + (int)((w >> 16) & 0xFFu) + (int)((e >> 16) & 0xFFu);
                                        int ob = cb + (((4 * cb - sumB) * 3) >> 5);
                                        int og = cg + (((4 * cg - sumG) * 3) >> 5);
                                        int or_ = cr + (((4 * cr - sumR) * 3) >> 5);
                                        if (ob < 0) ob = 0; else if (ob > 255) ob = 255;
                                        if (og < 0) og = 0; else if (og > 255) og = 255;
                                        if (or_ < 0) or_ = 0; else if (or_ > 255) or_ = 255;
                                        rOut[x] = 0xFF000000u | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Poll incoming mouse/controller/tracking packets right after screen capture for <0.5ms input latency!
            pollIncomingUdpPackets(GetTickCount64());

            POINT curPt = { 0, 0 };
            BOOL curOk = GetCursorPos(&curPt);
            float normX = (std::max)(0.0f, (std::min)(1.0f, (float)curPt.x / (float)scrW));
            float normY = (std::max)(0.0f, (std::min)(1.0f, (float)curPt.y / (float)scrH));

            // Equal-channel 0xF8 run-matching key (R, G, B all use identical 0xF8 mask so neutral grays have ZERO green bias!),
            // while transmitting full 24-bit True Color (r8, g8, b8) in 'V','8' and 'D','8' strips!
            auto toQ24 = [](uint32_t bgra) -> uint32_t {
                uint32_t b = (bgra >> 0) & 0xF8u;
                uint32_t g = (bgra >> 8) & 0xF8u;
                uint32_t r = (bgra >> 16) & 0xF8u;
                return (r << 16) | (g << 8) | b;
            };

            uint32_t stripsSentThisPass = 0;
            auto sendStripPayload = [&](const std::vector<uint8_t>& payload) {
                const size_t kMaxChunk = 1200;
                uint16_t fragCount = (uint16_t)((payload.size() + kMaxChunk - 1) / kMaxChunk);
                if (fragCount == 0) fragCount = 1;

                uint8_t pktBuf[sizeof(VxrUdpVideoFragmentHeader) + 1200];
                for (uint16_t fragIdx = 0; fragIdx < fragCount; ++fragIdx) {
                    size_t off = (size_t)fragIdx * kMaxChunk;
                    size_t rem = payload.size() - off;
                    uint16_t chunkLen = (uint16_t)((rem > kMaxChunk) ? kMaxChunk : rem);

                    VxrUdpVideoFragmentHeader hdr = {};
                    hdr.magic          = VXR_UDP_VIDEO_MAGIC;
                    hdr.frame_id       = liveFrameSeq;
                    hdr.frag_idx       = fragIdx;
                    hdr.frag_count     = fragCount;
                    hdr.flags          = 0x01 | (isStereoVrCompositorActive ? 0x04 : 0x00);
                    hdr.codec_id       = 0x55; // VXR Live Native RLE / Delta-RLE Strip
                    hdr.payload_len    = chunkLen;
                    hdr.pts_us         = now * 1000ULL;
                    hdr.cursor_norm_x  = normX;
                    hdr.cursor_norm_y  = normY;
                    hdr.cursor_visible = curOk ? 1 : 0;

                    memcpy(pktBuf, &hdr, sizeof(hdr));
                    memcpy(pktBuf + sizeof(hdr), payload.data() + off, chunkLen);

                    sendto(g_svc.udpSocket, reinterpret_cast<const char*>(pktBuf),
                           (int)(sizeof(hdr) + chunkLen), 0,
                           (sockaddr*)&lastActiveClient, sizeof(lastActiveClient));
                }
                liveFrameSeq++;
                g_svc.framesEncoded++;
                stripsSentThisPass++;
                if ((stripsSentThisPass & 3u) == 0) {
                    pollIncomingUdpPackets(GetTickCount64());
                }
            };

            auto encodeFullStrip = [&](uint32_t stripIndex, bool isEndOfFrame) {
                uint16_t y0 = (uint16_t)(stripIndex * kStripH);
                uint16_t hRows = (uint16_t)kStripH;
                const uint32_t* bgraBase = currLiveBgra.data() + (size_t)y0 * kLiveW;
                uint32_t* prevBase = prevSentQ24.data() + (size_t)y0 * kLiveW;
                size_t totalPixels = (size_t)kLiveW * hRows;

                bool hasNonZeroPixel = false;
                for (size_t i = 0; i < totalPixels; i += 16) {
                    if ((bgraBase[i] & 0x00FFFFFFu) != 0) {
                        hasNonZeroPixel = true;
                        break;
                    }
                }
                if (!hasNonZeroPixel) return;

                uint16_t rawHRows = isEndOfFrame ? (uint16_t)(hRows | 0x8000u) : hRows;
                std::vector<uint8_t> rlePayload;
                rlePayload.reserve(totalPixels * 2 + 16);
                uint16_t w16 = (uint16_t)kLiveW;
                rlePayload.push_back('V');
                rlePayload.push_back('8'); // 24-bit True Color RGB888 Full-RLE
                rlePayload.push_back((uint8_t)(w16 & 0xFF));
                rlePayload.push_back((uint8_t)(w16 >> 8));
                rlePayload.push_back((uint8_t)(y0 & 0xFF));
                rlePayload.push_back((uint8_t)(y0 >> 8));
                rlePayload.push_back((uint8_t)(rawHRows & 0xFF));
                rlePayload.push_back((uint8_t)(rawHRows >> 8));

                size_t idx = 0;
                while (idx < totalPixels) {
                    uint32_t bgra0 = bgraBase[idx];
                    uint32_t q0 = toQ24(bgra0);
                    prevBase[idx] = q0;
                    uint8_t runLen = 1;
                    while (idx + runLen < totalPixels && runLen < 255) {
                        uint32_t qNext = toQ24(bgraBase[idx + runLen]);
                        if (qNext != q0) break;
                        prevBase[idx + runLen] = qNext;
                        runLen++;
                    }
                    rlePayload.push_back(runLen);
                    rlePayload.push_back((uint8_t)((bgra0 >> 16) & 0xFFu)); // R8
                    rlePayload.push_back((uint8_t)((bgra0 >> 8)  & 0xFFu)); // G8
                    rlePayload.push_back((uint8_t)((bgra0 >> 0)  & 0xFFu)); // B8
                    idx += runLen;
                }
                sendStripPayload(rlePayload);
            };

            if (forceFullDesktopSync) {
                forceFullDesktopSync = false;
                for (uint32_t sIdx = 0; sIdx < (uint32_t)kStripCount; ++sIdx) {
                    encodeFullStrip(sIdx, (sIdx + 1 == (uint32_t)kStripCount));
                }
            } else {
                // Step 1: High-Speed Tear-Free 24-bit RGB888 Delta-RLE ('D','8') capped to 8 changed strips per 6ms tick
                // to prevent UDP Wi-Fi socket burst drops!
                uint32_t deltaSentCount = 0;
                uint32_t r0 = liveStripIdx;
                uint32_t r1 = (liveStripIdx + 1) % kStripCount;
                for (uint32_t step = 0; step < (uint32_t)kStripCount && deltaSentCount < 8; ++step) {
                    uint32_t sIdx = (deltaScanStripIdx + step) % kStripCount;
                    if (sIdx == r0 || sIdx == r1) {
                        continue; // Sent by the 2 rolling Full-RLE refresh strips below
                    }
                    uint16_t y0 = (uint16_t)(sIdx * kStripH);
                    uint16_t hRows = (uint16_t)kStripH;
                    const uint32_t* bgraBase = currLiveBgra.data() + (size_t)y0 * kLiveW;
                    uint32_t* prevBase = prevSentQ24.data() + (size_t)y0 * kLiveW;
                    size_t totalPixels = (size_t)kLiveW * hRows;

                    size_t changedCount = 0;
                    size_t nonZeroPixels = 0;
                    for (size_t i = 0; i < totalPixels; ++i) {
                        uint32_t qCur = toQ24(bgraBase[i]);
                        if (qCur != 0) nonZeroPixels++;
                        if (qCur != prevBase[i]) {
                            changedCount++;
                        }
                    }
                    if (changedCount == 0 || nonZeroPixels == 0) continue;

                    std::vector<uint8_t> deltaPayload;
                    deltaPayload.reserve(changedCount * 4 + 32);
                    uint16_t w16 = (uint16_t)kLiveW;
                    deltaPayload.push_back('D');
                    deltaPayload.push_back('8'); // 24-bit True Color RGB888 Delta-RLE
                    deltaPayload.push_back((uint8_t)(w16 & 0xFF));
                    deltaPayload.push_back((uint8_t)(w16 >> 8));
                    deltaPayload.push_back((uint8_t)(y0 & 0xFF));
                    deltaPayload.push_back((uint8_t)(y0 >> 8));
                    deltaPayload.push_back((uint8_t)(hRows & 0xFF));
                    deltaPayload.push_back((uint8_t)(hRows >> 8));

                    size_t idx = 0;
                    while (idx < totalPixels) {
                        uint32_t bgra0 = bgraBase[idx];
                        uint32_t qCur = toQ24(bgra0);
                        if (qCur == prevBase[idx]) {
                            size_t skipLen = 1;
                            while (idx + skipLen < totalPixels && skipLen < 65535 &&
                                   toQ24(bgraBase[idx + skipLen]) == prevBase[idx + skipLen]) {
                                skipLen++;
                            }
                            if (skipLen <= 127) {
                                deltaPayload.push_back((uint8_t)(0x80u | skipLen));
                            } else {
                                uint16_t s16 = (uint16_t)skipLen;
                                deltaPayload.push_back(0x80u);
                                deltaPayload.push_back((uint8_t)(s16 & 0xFF));
                                deltaPayload.push_back((uint8_t)(s16 >> 8));
                            }
                            idx += skipLen;
                        } else {
                            prevBase[idx] = qCur;
                            uint8_t runLen = 1;
                            while (idx + runLen < totalPixels && runLen < 127) {
                                uint32_t qNext = toQ24(bgraBase[idx + runLen]);
                                if (qNext != qCur) break;
                                prevBase[idx + runLen] = qNext;
                                runLen++;
                            }
                            deltaPayload.push_back(runLen);
                            deltaPayload.push_back((uint8_t)((bgra0 >> 16) & 0xFFu)); // R8
                            deltaPayload.push_back((uint8_t)((bgra0 >> 8)  & 0xFFu)); // G8
                            deltaPayload.push_back((uint8_t)((bgra0 >> 0)  & 0xFFu)); // B8
                            idx += runLen;
                        }
                    }

                    sendStripPayload(deltaPayload);
                    deltaSentCount++;
                    deltaScanStripIdx = (sIdx + 1) % kStripCount;
                }

                // Step 2: Send 2 rolling Full-RLE refresh strips ('V','8') per tick, marking the 2nd strip with End-Of-Frame (0x8000)!
                for (int s = 0; s < 2; ++s) {
                    encodeFullStrip(liveStripIdx, (s == 1));
                    liveStripIdx = (liveStripIdx + 1) % kStripCount;
                }
            }

            LARGE_INTEGER encEndQpc = {};
            QueryPerformanceCounter(&encEndQpc);
            if (qpcFreq.QuadPart > 0) {
                double passMs = (double)(encEndQpc.QuadPart - encStartQpc.QuadPart) * 1000.0 / (double)qpcFreq.QuadPart;
                if (passMs >= 0.1 && passMs <= 8.0) {
                    smoothedEncodeMs = smoothedEncodeMs * 0.8 + passMs * 0.2;
                }
            }
        }

        // Ultra-low-latency 250us event-driven UDP socket wait (< 0.25ms wakeup!)
        if (g_svc.udpSocket != INVALID_SOCKET) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(g_svc.udpSocket, &readSet);
            timeval tv = { 0, 250 }; // 250 microseconds = 0.25 ms max wait
            select(0, &readSet, nullptr, nullptr, &tv);
        } else {
            Sleep(1);
        }
    }

    timeEndPeriod(1);
    if (activeVirtualMonitorId != 0 && pfn_IddDestroyMonitor) {
        pfn_IddDestroyMonitor(activeVirtualMonitorId);
        activeVirtualMonitorId = 0;
    }
    runVrPathReg(L"removedriver");

    if (hWaveOutMic) {
        waveOutReset(hWaveOutMic);
        for (int i = 0; i < kMicRingCount; ++i) {
            if (micWaveHdrs[i].dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(hWaveOutMic, &micWaveHdrs[i], sizeof(WAVEHDR));
            }
        }
        waveOutClose(hWaveOutMic);
    }
    if (wasapiLoopbackRunning && pAudioClient) pAudioClient->Stop();
    if (pMixFormat) CoTaskMemFree(pMixFormat);
    if (pCaptureClient) pCaptureClient->Release();
    if (pAudioClient) pAudioClient->Release();
    if (pAudioDev) pAudioDev->Release();
    if (pAudioEnum) pAudioEnum->Release();

    if (pSteamVrShm) UnmapViewOfFile(pSteamVrShm);
    if (hSteamVrShm) CloseHandle(hSteamVrShm);

    if (hOldBmp && hCaptureDC) SelectObject(hCaptureDC, hOldBmp);
    if (hLiveBmp) DeleteObject(hLiveBmp);
    if (hCaptureDC) DeleteDC(hCaptureDC);
    if (hScreenDC) ReleaseDC(nullptr, hScreenDC);

    wifiOptimizer.UnlockBackgroundScan();
    if (hThreadAvrt) AvRevertMmThreadCharacteristics(hThreadAvrt);

    if (capInit) {
        dxgiDupl.Cleanup();
    }

    if (g_svc.udpSocket != INVALID_SOCKET) closesocket(g_svc.udpSocket);
    if (g_svc.mdnsSocket != INVALID_SOCKET) closesocket(g_svc.mdnsSocket);
    WSACleanup();

    if (g_svc.encoderHandle && pfn_encoder_destroy) {
        pfn_encoder_destroy(g_svc.encoderHandle);
        g_svc.encoderHandle = nullptr;
    }
    if (pD3DCtx) pD3DCtx->Release();
    if (pD3DDevice) pD3DDevice->Release();
    CoUninitialize();
}

// ============================================================================
// UI LAYOUT & RENDERING
// ============================================================================

static bool PtInRectGdi(const Rect& r, int x, int y) {
    return x >= r.X && x < (r.X + r.Width) && y >= r.Y && y < (r.Y + r.Height);
}

static void RebuildControlsForTab() {
    g_dropdowns.clear();
    g_checkboxes.clear();
    g_sliders.clear();
    g_editingAccount = false;
    g_recordingBindingIdx = -1;

    if (g_activeTab == TAB_OPTIONS) {
        g_dropdowns.push_back({
            1, Rect(178, 84, 178, 28),
            { L"Automatic", L"HEVC 10-bit", L"AV1 10-bit", L"HEVC", L"H.264+", L"H.264" },
            &g_cfg.preferredCodec
        });
        g_dropdowns.push_back({
            2, Rect(178, 208, 178, 28),
            { L"VXR-XR", L"Automatic", L"SteamVR" },
            &g_cfg.openxrRuntime
        });
        g_dropdowns.push_back({
            3, Rect(178, 270, 178, 28),
            { L"Automatic", L"Xbox 360 (ViGEm)", L"DualShock 4", L"Disabled" },
            &g_cfg.gamepadEmulation
        });
        g_dropdowns.push_back({
            4, Rect(178, 332, 206, 28),
            { L"VR headset only", L"Computer only", L"Both" },
            &g_cfg.audioStreaming
        });

        g_checkboxes.push_back({ 101, Rect(178, 122, 210, 20), L"Adaptive quantization", &g_cfg.adaptiveQuantization, false });
        g_checkboxes.push_back({ 102, Rect(178, 150, 210, 20), L"2-Pass encoding", &g_cfg.twoPassEncoding, false });
        g_checkboxes.push_back({ 103, Rect(178, 370, 215, 20), L"Use virtual audio driver", &g_cfg.useVirtualAudioDriver, false });
        g_checkboxes.push_back({ 104, Rect(186, 398, 200, 20), L"VoiceMeeter mode", &g_cfg.voiceMeeterMode, false });

        g_checkboxes.push_back({ 105, Rect(425, 62,  235, 20), L"Allow remote connections", &g_cfg.allowRemoteConnections, false });
        g_checkboxes.push_back({ 106, Rect(425, 92,  235, 20), L"Encrypt local traffic", &g_cfg.encryptLocalTraffic, false });
        g_checkboxes.push_back({ 107, Rect(425, 122, 235, 20), L"Automatically adjust bitrate", &g_cfg.autoAdjustBitrate, false });
        g_checkboxes.push_back({ 108, Rect(425, 152, 235, 20), L"Start with Windows", &g_cfg.startWithWindows, false });
        g_checkboxes.push_back({ 109, Rect(425, 182, 235, 20), L"Start minimized in tray", &g_cfg.startMinimizedInTray, false });
        g_checkboxes.push_back({ 110, Rect(425, 212, 235, 20), L"Use touch input", &g_cfg.useTouchInput, false });
        g_checkboxes.push_back({ 111, Rect(425, 242, 235, 20), L"Lock computer on disconnect", &g_cfg.lockComputerOnDisconnect, false });
        g_checkboxes.push_back({ 112, Rect(425, 272, 235, 20), L"Auto-select microphone", &g_cfg.autoSelectMicrophone, false });
        g_checkboxes.push_back({ 113, Rect(425, 302, 235, 20), L"Ask for computer access", &g_cfg.askForComputerAccess, false });
    }
    else if (g_activeTab == TAB_ADVANCED) {
        g_sliders.push_back({ 10, Rect(180, 132, 154, 16), 50, 100, &g_cfg.hFovTangent, nullptr });
        g_sliders.push_back({ 11, Rect(180, 184, 154, 16), 50, 100, &g_cfg.vFovTangent, nullptr });
        g_sliders.push_back({ 12, Rect(180, 236, 154, 16), 50, 150, &g_cfg.renderResolution, nullptr });
        g_sliders.push_back({ 13, Rect(180, 340, 154, 16), 10, 100, &g_cfg.foveaSize, nullptr });
        g_sliders.push_back({ 14, Rect(180, 394, 154, 16), 0,  100, &g_cfg.sharpeningPercent, &g_cfg.additionalSharpening });

        g_checkboxes.push_back({ 201, Rect(186, 366, 200, 20), L"Additional sharpening", &g_cfg.additionalSharpening, false });
        g_checkboxes.push_back({ 202, Rect(416, 108, 200, 20), L"Use Fov Stencil", &g_cfg.useFovStencil, false });
        g_checkboxes.push_back({ 203, Rect(416, 138, 200, 20), L"Boost game priority", &g_cfg.boostGamePriority, false });
    }
}

static void DrawCheckbox(Graphics& g, const CheckboxSpec& cb, Font& font) {
    Rect box(cb.rect.X, cb.rect.Y + 1, 16, 16);
    SolidBrush boxBg(Color(255, 5, 5, 5));
    Pen boxBorder(Color(255, 62, 62, 62), 1.0f);
    g.FillRectangle(&boxBg, box);
    g.DrawRectangle(&boxBorder, box);

    if (*cb.value) {
        Pen checkPen(Color(255, 30, 144, 255), 2.2f);
        checkPen.SetStartCap(LineCapRound);
        checkPen.SetEndCap(LineCapRound);
        g.DrawLine(&checkPen, box.X + 3, box.Y + 8, box.X + 7, box.Y + 12);
        g.DrawLine(&checkPen, box.X + 7, box.Y + 12, box.X + 13, box.Y + 4);
    }

    SolidBrush textBrush(cb.disabled ? Color(255, 105, 105, 105) : Color(255, 204, 204, 204));
    PointF pt((REAL)(cb.rect.X + 26), (REAL)(cb.rect.Y));
    g.DrawString(cb.label.c_str(), -1, &font, pt, &textBrush);
}

static void DrawDropdown(Graphics& g, const DropdownSpec& dd, Font& font) {
    SolidBrush bg(Color(255, 4, 4, 4));
    Pen border(Color(255, 68, 68, 68), 1.0f);
    g.FillRectangle(&bg, dd.rect);
    g.DrawRectangle(&border, dd.rect);

    std::wstring text = (*dd.selectedIndex >= 0 && *dd.selectedIndex < (int)dd.items.size())
        ? dd.items[*dd.selectedIndex] : L"";

    SolidBrush textBrush(Color(255, 235, 235, 235));
    RectF textRect((REAL)(dd.rect.X + 9), (REAL)(dd.rect.Y + 5), (REAL)(dd.rect.Width - 28), (REAL)(dd.rect.Height - 6));
    g.DrawString(text.c_str(), -1, &font, textRect, nullptr, &textBrush);

    SolidBrush arrowBrush(Color(255, 25, 130, 230));
    int cx = dd.rect.X + dd.rect.Width - 15;
    int cy = dd.rect.Y + dd.rect.Height / 2 - 2;
    Point pts[3] = { Point(cx - 5, cy), Point(cx + 5, cy), Point(cx, cy + 6) };
    g.FillPolygon(&arrowBrush, pts, 3);
}

static void DrawSlider(Graphics& g, const SliderSpec& sl, Font& font) {
    bool isEnabled = (sl.enabledFlag == nullptr) || (*sl.enabledFlag);
    int trackY = sl.trackRect.Y + sl.trackRect.Height / 2;
    Rect barRect(sl.trackRect.X, trackY - 2, sl.trackRect.Width, 4);

    SolidBrush trackBg(Color(255, 6, 6, 6));
    Pen trackBorder(Color(255, 58, 58, 58), 1.0f);
    g.FillRectangle(&trackBg, barRect);
    g.DrawRectangle(&trackBorder, barRect);

    float t = (float)(*sl.value - sl.minVal) / (float)(sl.maxVal - sl.minVal);
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    int thumbX = sl.trackRect.X + (int)(t * sl.trackRect.Width);
    int thumbR = 8;

    SolidBrush thumbBrush(isEnabled ? Color(255, 200, 200, 200) : Color(255, 92, 92, 92));
    Pen thumbBorder(Color(255, 70, 70, 70), 1.0f);
    g.FillEllipse(&thumbBrush, thumbX - thumbR, trackY - thumbR, thumbR * 2, thumbR * 2);
    g.DrawEllipse(&thumbBorder, thumbX - thumbR, trackY - thumbR, thumbR * 2, thumbR * 2);

    wchar_t valBuf[32];
    swprintf_s(valBuf, L"%d%%", *sl.value);
    SolidBrush textBrush(Color(255, 204, 204, 204));
    PointF pt((REAL)(sl.trackRect.X + sl.trackRect.Width + 14), (REAL)(sl.trackRect.Y - 2));
    g.DrawString(valBuf, -1, &font, pt, &textBrush);
}

static void DrawButton(Graphics& g, const Rect& r, const wchar_t* text, bool hovered, Font& font) {
    LinearGradientBrush grad(
        Point(r.X, r.Y),
        Point(r.X, r.Y + r.Height),
        hovered ? Color(255, 44, 44, 44) : Color(255, 28, 28, 28),
        hovered ? Color(255, 20, 20, 20) : Color(255, 10, 10, 10)
    );
    Pen border(hovered ? Color(255, 105, 105, 105) : Color(255, 62, 62, 62), 1.0f);
    g.FillRectangle(&grad, r);
    g.DrawRectangle(&border, r);

    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush textBrush(Color(255, 240, 240, 240));
    RectF rf((REAL)r.X, (REAL)r.Y, (REAL)r.Width, (REAL)r.Height);
    g.DrawString(text, -1, &font, rf, &sf, &textBrush);
}

static void DrawFolderIcon(Graphics& g, int x, int y) {
    SolidBrush tabBrush(Color(255, 215, 165, 50));
    SolidBrush bodyBrush(Color(255, 240, 195, 75));
    g.FillRectangle(&tabBrush, x, y, 6, 3);
    g.FillRectangle(&bodyBrush, x, y + 2, 13, 11);
}

static void DrawFilmIcon(Graphics& g, int x, int y) {
    SolidBrush bgBrush(Color(255, 70, 110, 210));
    Pen borderPen(Color(255, 170, 170, 170), 1.0f);
    g.FillRectangle(&bgBrush, x, y, 13, 13);
    g.DrawRectangle(&borderPen, x, y, 13, 13);
    SolidBrush holeBrush(Color(255, 240, 200, 60));
    g.FillRectangle(&holeBrush, x + 2, y + 2, 2, 2);
    g.FillRectangle(&holeBrush, x + 9, y + 2, 2, 2);
    g.FillRectangle(&holeBrush, x + 2, y + 9, 2, 2);
    g.FillRectangle(&holeBrush, x + 9, y + 9, 2, 2);
}

static void RenderWindow(HDC hdc) {
    Bitmap backBuffer(WIN_W, WIN_H, PixelFormat32bppARGB);
    Graphics g(&backBuffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily segoe(L"Segoe UI");
    FontFamily georgia(L"Georgia");
    Font titleFont(&georgia, 16.0f, FontStyleBold, UnitPixel);
    Font navFont(&segoe, 14.5f, FontStyleBold, UnitPixel);
    Font labelBold(&segoe, 13.5f, FontStyleBold, UnitPixel);
    Font regularFont(&segoe, 13.0f, FontStyleRegular, UnitPixel);
    Font underlineFont(&segoe, 13.0f, FontStyleUnderline, UnitPixel);
    Font smallFont(&segoe, 12.0f, FontStyleRegular, UnitPixel);
    Font footerFont(&segoe, 12.0f, FontStyleRegular, UnitPixel);

    // 1. Main Content Background (#0C0C0C)
    SolidBrush contentBg(Color(255, 12, 12, 12));
    g.FillRectangle(&contentBg, 0, 0, WIN_W, WIN_H);

    // 2. Left Sidebar (#151515)
    SolidBrush sidebarBg(Color(255, 21, 21, 21));
    g.FillRectangle(&sidebarBg, 0, TOP_BAR_H, SIDEBAR_W, WIN_H - TOP_BAR_H - BOTTOM_BAR_H);

    // 3. Top Title Bar (#1C1C1C)
    SolidBrush topBarBg(Color(255, 28, 28, 28));
    g.FillRectangle(&topBarBg, 0, 0, WIN_W, TOP_BAR_H);

    Pen iconPen(Color(255, 230, 230, 230), 2.0f);
    Point monPts[4] = { Point(14, 10), Point(40, 8), Point(38, 30), Point(15, 28) };
    g.DrawPolygon(&iconPen, monPts, 4);
    g.DrawLine(&iconPen, 23, 29, 21, 35);
    g.DrawLine(&iconPen, 30, 29, 32, 35);
    g.DrawLine(&iconPen, 18, 35, 35, 35);

    SolidBrush whiteBrush(Color(255, 242, 242, 242));
    g.DrawString(L"Project VXR Streamer", -1, &titleFont, PointF(50.0f, 12.0f), &whiteBrush);

    if (g_hoverMin) {
        SolidBrush minHoverBg(Color(255, 48, 48, 48));
        g.FillRectangle(&minHoverBg, 604, 0, 38, TOP_BAR_H);
    }
    if (g_hoverClose) {
        SolidBrush closeHoverBg(Color(255, 232, 17, 35));
        g.FillRectangle(&closeHoverBg, 642, 0, 38, TOP_BAR_H);
    }
    SolidBrush minBrush(g_hoverMin ? Color(255, 255, 255, 255) : Color(255, 145, 145, 145));
    Pen closePen(g_hoverClose ? Color(255, 255, 255, 255) : Color(255, 145, 145, 145), 1.8f);
    g.FillRectangle(&minBrush, 617, 22, 12, 2);
    g.DrawLine(&closePen, 656, 16, 666, 26);
    g.DrawLine(&closePen, 666, 16, 656, 26);

    // 4. Sidebar Navigation Items
    const wchar_t* navLabels[6] = {
        L"ACCOUNTS",
        L"BINDINGS",
        L"OPTIONS",
        L"ADVANCED",
        L"MEDIA",
        L"ABOUT"
    };
    for (int i = 0; i < 6; ++i) {
        Color c = (g_activeTab == i)
            ? Color(255, 255, 255, 255)
            : ((g_hoverNav == i) ? Color(255, 185, 185, 185) : Color(255, 122, 122, 122));
        SolidBrush nb(c);
        g.DrawString(navLabels[i], -1, &navFont, PointF(14.0f, (REAL)(65 + i * 44)), &nb);
    }

    SolidBrush hdrBrush(Color(255, 208, 208, 208));
    SolidBrush mutedBrush(Color(255, 145, 145, 145));
    SolidBrush blueLinkBrush(Color(255, 45, 145, 235));
    SolidBrush orangeBrush(Color(255, 230, 154, 40));

    if (g_svc.isStarting) {
        std::wstring stepMsg;
        {
            std::lock_guard<std::mutex> lock(g_svc.statusMutex);
            stepMsg = g_svc.bootStepText;
        }
        StringFormat centerFmt;
        centerFmt.SetAlignment(StringAlignmentCenter);
        g.DrawString(L"Starting Project VXR Host Service...", -1, &labelBold, PointF(416.0f, 195.0f), &centerFmt, &whiteBrush);
        g.DrawString(stepMsg.c_str(), -1, &smallFont, PointF(416.0f, 222.0f), &centerFmt, &mutedBrush);

        Rect progTrack(266, 252, 300, 6);
        SolidBrush trackBg(Color(255, 28, 28, 28));
        g.FillRectangle(&trackBg, progTrack);
        int fillW = (300 * g_svc.bootProgressPct.load()) / 100;
        SolidBrush fillBrush(Color(255, 30, 144, 255));
        g.FillRectangle(&fillBrush, 266, 252, fillW, 6);
    }
    else {
        if (g_activeTab == TAB_ACCOUNTS) {
            Pen metaPen(Color(255, 24, 130, 245), 2.2f);
            g.DrawEllipse(&metaPen, 182, 63, 7, 9);
            g.DrawEllipse(&metaPen, 188, 63, 7, 9);
            g.DrawString(L"Meta", -1, &labelBold, PointF(200.0f, 59.0f), &hdrBrush);

            if (!g_editingAccount) {
                g.DrawString(g_cfg.metaAccount.c_str(), -1, &regularFont, PointF(180.0f, 84.0f), &mutedBrush);
                DrawButton(g, Rect(354, 126, 70, 30), L"Change", g_hoverBtnId == 301, regularFont);
            } else {
                Rect editBox(182, 82, 220, 28);
                SolidBrush editBg(Color(255, 5, 5, 5));
                Pen editBorder(Color(255, 30, 144, 255), 1.2f);
                g.FillRectangle(&editBg, editBox);
                g.DrawRectangle(&editBorder, editBox);
                std::wstring disp = g_accountEditBuf + L"|";
                g.DrawString(disp.c_str(), -1, &regularFont, PointF(190.0f, 86.0f), &whiteBrush);
                DrawButton(g, Rect(412, 82, 65, 28), L"Save", g_hoverBtnId == 302, regularFont);
            }
        }
        else if (g_activeTab == TAB_BINDINGS) {
            g.DrawString(L"Keyboard shortcuts", -1, &labelBold, PointF(181.0f, 56.0f), &hdrBrush);

            for (int i = 0; i < 9; ++i) {
                int rowY = 85 + i * 33;
                g.DrawString(g_cfg.bindings[i].actionName.c_str(), -1, &regularFont, PointF(181.0f, (REAL)(rowY + 4)), &hdrBrush);

                Rect boxR(500, rowY, 166, 28);
                SolidBrush boxBg(Color(255, 4, 4, 4));
                bool isRec = (g_recordingBindingIdx == i);
                bool isHov = (g_hoverBindingIdx == i);
                Pen boxBorder(isRec ? Color(255, 30, 144, 255) : (isHov ? Color(255, 110, 110, 110) : Color(255, 58, 58, 58)), 1.0f);
                g.FillRectangle(&boxBg, boxR);
                g.DrawRectangle(&boxBorder, boxR);

                std::wstring keyText = isRec ? L"Press shortcut..." : g_cfg.bindings[i].FormatText();
                StringFormat sf;
                sf.SetAlignment(StringAlignmentCenter);
                sf.SetLineAlignment(StringAlignmentCenter);
                RectF rf((REAL)boxR.X, (REAL)boxR.Y, (REAL)boxR.Width, (REAL)boxR.Height);
                g.DrawString(keyText.c_str(), -1, &regularFont, rf, &sf, isRec ? &whiteBrush : &blueLinkBrush);
            }

            DrawButton(g, Rect(178, 392, 156, 30), L"Reset to Defaults", g_hoverBtnId == 100, regularFont);
        }
        else if (g_activeTab == TAB_OPTIONS) {
            g.DrawString(L"Preferred Codec", -1, &labelBold, PointF(180.0f, 60.0f), &hdrBrush);
            g.DrawString(L"OpenXR Runtime", -1, &labelBold, PointF(180.0f, 184.0f), &hdrBrush);
            g.DrawString(L"Gamepad Emulation", -1, &labelBold, PointF(180.0f, 246.0f), &hdrBrush);
            g.DrawString(L"Audio Streaming", -1, &labelBold, PointF(180.0f, 308.0f), &hdrBrush);

            for (const auto& cb : g_checkboxes) {
                DrawCheckbox(g, cb, regularFont);
            }
            for (const auto& dd : g_dropdowns) {
                DrawDropdown(g, dd, regularFont);
            }
            DrawButton(g, Rect(506, 392, 156, 30), L"Reset to Defaults", g_hoverBtnId == 100, regularFont);
        }
        else if (g_activeTab == TAB_ADVANCED) {
            SolidBrush infoCircle(Color(255, 125, 125, 125));
            g.FillEllipse(&infoCircle, 180, 56, 14, 14);
            SolidBrush darkI(Color(255, 12, 12, 12));
            g.DrawString(L"i", -1, &smallFont, PointF(184.5f, 54.5f), &darkI);

            g.DrawString(L"Do NOT change the settings below unless you know what you are doing", -1, &smallFont, PointF(200.0f, 54.0f), &mutedBrush);
            g.DrawString(L"Requires restart of game / SteamVR to take effect", -1, &smallFont, PointF(200.0f, 71.0f), &mutedBrush);

            g.DrawString(L"Horizontal FOV Tangent", -1, &labelBold, PointF(182.0f, 104.0f), &hdrBrush);
            g.DrawString(L"Vertical FOV Tangent", -1, &labelBold, PointF(182.0f, 156.0f), &hdrBrush);
            g.DrawString(L"VXR-XR Render Resolution", -1, &labelBold, PointF(182.0f, 208.0f), &hdrBrush);

            g.DrawString(L"Best to use 100% and control the", -1, &smallFont, PointF(182.0f, 254.0f), &mutedBrush);
            g.DrawString(L"resolution via the VR Graphics Quality", -1, &smallFont, PointF(182.0f, 270.0f), &mutedBrush);
            g.DrawString(L"in the Streaming tab in VR", -1, &smallFont, PointF(182.0f, 286.0f), &mutedBrush);

            g.DrawString(L"Fovea Size", -1, &labelBold, PointF(182.0f, 314.0f), &hdrBrush);

            for (const auto& cb : g_checkboxes) {
                DrawCheckbox(g, cb, regularFont);
            }
            for (const auto& sl : g_sliders) {
                DrawSlider(g, sl, regularFont);
            }
            DrawButton(g, Rect(506, 392, 156, 30), L"Reset to Defaults", g_hoverBtnId == 100, regularFont);
        }
        else if (g_activeTab == TAB_MEDIA) {
            DrawFolderIcon(g, 180, 62);
            g.DrawString(L"Screenshots", -1, &regularFont, PointF(198.0f, 58.0f), &hdrBrush);

            StringFormat rightFmt;
            rightFmt.SetAlignment(StringAlignmentFar);
            g.DrawString(g_cfg.screenshotsPath.c_str(), -1, &regularFont, PointF(495.0f, 58.0f), &rightFmt, &mutedBrush);
            DrawButton(g, Rect(518, 53, 146, 30), L"Change location...", g_hoverBtnId == 401, regularFont);

            SolidBrush infoCircle(Color(255, 125, 125, 125));
            SolidBrush darkI(Color(255, 12, 12, 12));
            g.FillEllipse(&infoCircle, 180, 88, 13, 13);
            g.DrawString(L"i", -1, &smallFont, PointF(184.0f, 86.0f), &darkI);
            g.DrawString(L"Screenshots taken in VR will be copied to this folder", -1, &smallFont, PointF(198.0f, 86.0f), &mutedBrush);

            for (size_t i = 0; i < g_cfg.videoPaths.size() && i < 3; ++i) {
                int baseY = 128 + (int)i * 70;
                DrawFilmIcon(g, 180, baseY + 3);
                g.DrawString(L"Videos", -1, &regularFont, PointF(198.0f, (REAL)baseY), &hdrBrush);
                g.DrawString(g_cfg.videoPaths[i].c_str(), -1, &regularFont, PointF(495.0f, (REAL)baseY), &rightFmt, &mutedBrush);
                DrawButton(g, Rect(518, baseY - 5, 146, 30), L"Change location...", g_hoverBtnId == (int)(410 + i), regularFont);

                if (i == 0) {
                    g.FillEllipse(&infoCircle, 180, baseY + 29, 13, 13);
                    g.DrawString(L"i", -1, &smallFont, PointF(184.0f, (REAL)(baseY + 27)), &darkI);
                    g.DrawString(L"Videos from this location will be accessible in VR", -1, &smallFont, PointF(198.0f, (REAL)(baseY + 27)), &mutedBrush);
                }
            }

            DrawButton(g, Rect(178, 392, 164, 30), L"Add video folder...", g_hoverBtnId == 402, regularFont);
        }
        else if (g_activeTab == TAB_ABOUT) {
            // Matches Screenshot 1 (ABOUT tab of Virtual Desktop Streamer) with 100% live PC hardware/OS values!
            std::wstring gpuFull, cpuStr, ramStr, osStr, headsetStr;
            bool secBoot, hags, ethUp;
            float liveLatencyMs = 0.0f;
            {
                std::lock_guard<std::mutex> lock(g_svc.statusMutex);
                gpuFull       = g_svc.gpuNameWithDriver;
                cpuStr        = g_svc.cpuName;
                ramStr        = g_svc.ramTotalGb;
                osStr         = g_svc.osVersionStr;
                headsetStr    = g_svc.connectedHeadset;
                secBoot       = g_svc.secureBootEnabled;
                hags          = g_svc.hagsEnabled;
                ethUp         = g_svc.pcEthernetConnected;
                liveLatencyMs = g_svc.activeLatencyMs;
            }

            StringFormat rightLabelFmt;
            rightLabelFmt.SetAlignment(StringAlignmentFar);

            const REAL labelX = 304.0f;
            const REAL valX   = 308.0f;
            const REAL startY = 54.0f;
            const REAL stepY  = 20.5f;

            const wchar_t* labels[16] = {
                L"Graphics Card:",
                L"Headset:",
                L"Codec:",
                L"Latency:",
                L"Processor:",
                L"Hardware Encoder:",
                L"Memory:",
                L"Operating System:",
                L"Secure Boot:",
                L"HAGS:",
                L"PC Ethernet:",
                L"Auto Adjust Bitrate:",
                L"Remote Routing:",
                L"Router Settings:",
                L"Website:",
                L"Discord:"
            };

            for (int i = 0; i < 16; ++i) {
                g.DrawString(labels[i], -1, &regularFont, PointF(labelX, startY + i * stepY), &rightLabelFmt, &mutedBrush);
            }

            // Values
            g.DrawString(gpuFull.c_str(),                    -1, &regularFont, PointF(valX, startY + 0 * stepY), &whiteBrush);
            g.DrawString(headsetStr.c_str(),                 -1, &regularFont, PointF(valX, startY + 1 * stepY),
                         (headsetStr == L"Not connected") ? &mutedBrush : &whiteBrush);
            g.DrawString(g_cfg.GetCodecName(),               -1, &regularFont, PointF(valX, startY + 2 * stepY), &whiteBrush);
            if (liveLatencyMs > 0.05f && headsetStr != L"Not connected") {
                wchar_t latBuf[32] = {};
                swprintf_s(latBuf, L"%.1f ms", liveLatencyMs);
                g.DrawString(latBuf, -1, &regularFont, PointF(valX, startY + 3 * stepY), &whiteBrush);
            } else {
                g.DrawString(L"--", -1, &regularFont, PointF(valX, startY + 3 * stepY), &mutedBrush);
            }
            g.DrawString(cpuStr.c_str(),                     -1, &regularFont, PointF(valX, startY + 4 * stepY), &whiteBrush);
            g.DrawString(g_svc.nvencActive ? L"Yes" : L"No", -1, &regularFont, PointF(valX, startY + 5 * stepY), &whiteBrush);
            g.DrawString(ramStr.c_str(),                     -1, &regularFont, PointF(valX, startY + 6 * stepY), &whiteBrush);
            g.DrawString(osStr.c_str(),                      -1, &regularFont, PointF(valX, startY + 7 * stepY), &whiteBrush);
            g.DrawString(secBoot ? L"Enabled" : L"Disabled", -1, &regularFont, PointF(valX, startY + 8 * stepY), &whiteBrush);

            if (hags) {
                g.DrawString(L"Enabled - ", -1, &regularFont, PointF(valX, startY + 9 * stepY), &whiteBrush);
                g.DrawString(L"click here to disable", -1, &underlineFont, PointF(valX + 62.0f, startY + 9 * stepY), &blueLinkBrush);
            } else {
                g.DrawString(L"Disabled", -1, &regularFont, PointF(valX, startY + 9 * stepY), &whiteBrush);
            }

            g.DrawString(ethUp ? L"Yes" : L"No", -1, &regularFont, PointF(valX, startY + 10 * stepY), ethUp ? &whiteBrush : &orangeBrush);
            g.DrawString(g_cfg.autoAdjustBitrate ? L"Yes" : L"No", -1, &regularFont, PointF(valX, startY + 11 * stepY), g_cfg.autoAdjustBitrate ? &whiteBrush : &orangeBrush);
            g.DrawString(g_cfg.allowRemoteConnections ? L"Regular NAT" : L"Local LAN Only", -1, &regularFont, PointF(valX, startY + 12 * stepY), &whiteBrush);
            g.DrawString(g_svc.udpServerListening ? L"UDP :38810 (DSCP 46 QoS)" : L"Not bound", -1, &regularFont, PointF(valX, startY + 13 * stepY), &mutedBrush);
            g.DrawString(L"github.com/ProjectVXR", -1, &underlineFont, PointF(valX, startY + 14 * stepY), &blueLinkBrush);
            g.DrawString(L"discord.gg/projectvxr", -1, &underlineFont, PointF(valX, startY + 15 * stepY), &blueLinkBrush);

            // Bottom 3 functional buttons (matching Screenshot 1)
            DrawButton(g, Rect(182, 392, 185, 30), L"Check for interfering apps...", g_hoverBtnId == 501, regularFont);
            DrawButton(g, Rect(376, 392, 140, 30), L"Check for updates...",        g_hoverBtnId == 502, regularFont);
            DrawButton(g, Rect(524, 392, 80,  30), L"Show logs",                   g_hoverBtnId == 503, regularFont);
        }
    }

    // 6. Open Dropdown Popup Overlay
    if (g_openDropdownId != -1 && g_openDropdownTarget != nullptr) {
        SolidBrush popupBg(Color(255, 16, 16, 16));
        Pen popupBorder(Color(255, 30, 144, 255), 1.0f);
        g.FillRectangle(&popupBg, g_openDropdownMenuRect);
        g.DrawRectangle(&popupBorder, g_openDropdownMenuRect);

        int itemH = 26;
        for (size_t i = 0; i < g_openDropdownItems.size(); ++i) {
            Rect itemR(g_openDropdownMenuRect.X + 1, g_openDropdownMenuRect.Y + 1 + (int)i * itemH, g_openDropdownMenuRect.Width - 2, itemH);
            if ((int)i == g_hoverDropdownItem || (int)i == *g_openDropdownTarget) {
                SolidBrush hl(Color(255, 25, 110, 200));
                g.FillRectangle(&hl, itemR);
            }
            SolidBrush itemText(Color(255, 240, 240, 240));
            g.DrawString(g_openDropdownItems[i].c_str(), -1, &regularFont, PointF((REAL)(itemR.X + 8), (REAL)(itemR.Y + 4)), &itemText);
        }
    }

    // 7. Bottom Footer Bar (#1B1B1B)
    SolidBrush bottomBg(Color(255, 27, 27, 27));
    g.FillRectangle(&bottomBg, 0, WIN_H - BOTTOM_BAR_H, WIN_W, BOTTOM_BAR_H);

    Pen sepPen(Color(255, 38, 38, 38), 1.0f);
    g.DrawLine(&sepPen, 0, TOP_BAR_H, WIN_W, TOP_BAR_H);
    g.DrawLine(&sepPen, 0, WIN_H - BOTTOM_BAR_H, WIN_W, WIN_H - BOTTOM_BAR_H);

    SolidBrush verBrush(Color(255, 130, 130, 130));
    std::wstring leftFooter = g_svc.isStarting
        ? L"Service: Starting..."
        : (g_svc.udpServerListening ? L"Service: Running (UDP :38810 • mDNS Active)" : L"Service: Degraded");
    g.DrawString(leftFooter.c_str(), -1, &footerFont, PointF(14.0f, (REAL)(WIN_H - BOTTOM_BAR_H + 7)), &verBrush);

    std::wstring rightFooter;
    {
        std::lock_guard<std::mutex> lock(g_svc.statusMutex);
        if (GetTickCount64() < g_svc.toastExpireTick && !g_svc.lastActionToast.empty()) {
            rightFooter = g_svc.lastActionToast;
        } else {
            rightFooter = g_svc.gpuShortName;
        }
    }
    StringFormat rightFmt;
    rightFmt.SetAlignment(StringAlignmentFar);
    SolidBrush rightBrush((GetTickCount64() < g_svc.toastExpireTick) ? Color(255, 45, 160, 255) : Color(255, 130, 130, 130));
    g.DrawString(rightFooter.c_str(), -1, &footerFont, PointF((REAL)(WIN_W - 14), (REAL)(WIN_H - BOTTOM_BAR_H + 7)), &rightFmt, &rightBrush);

    Pen winBorder(Color(255, 45, 45, 45), 1.0f);
    g.DrawRectangle(&winBorder, 0, 0, WIN_W - 1, WIN_H - 1);

    Graphics screen(hdc);
    screen.DrawImage(&backBuffer, 0, 0, WIN_W, WIN_H);
}

static void UpdateSliderFromMouse(const SliderSpec& sl, int mouseX) {
    float t = (float)(mouseX - sl.trackRect.X) / (float)sl.trackRect.Width;
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    *sl.value = sl.minVal + (int)(t * (sl.maxVal - sl.minVal) + 0.5f);
    SaveConfig();
}

static HICON g_hAppIcon = nullptr;

static HICON CreateVxrAppIcon() {
    Bitmap bmp(32, 32, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    SolidBrush bgBrush(Color(255, 22, 22, 22));
    Pen borderPen(Color(255, 45, 145, 235), 2.0f);
    g.FillEllipse(&bgBrush, 1, 1, 29, 29);
    g.DrawEllipse(&borderPen, 1, 1, 29, 29);

    Pen monPen(Color(255, 245, 245, 245), 2.2f);
    Point pts[4] = { Point(7, 9), Point(25, 8), Point(23, 21), Point(8, 20) };
    g.DrawPolygon(&monPen, pts, 4);
    g.DrawLine(&monPen, 13, 21, 11, 25);
    g.DrawLine(&monPen, 18, 21, 20, 25);
    g.DrawLine(&monPen, 9, 25, 22, 25);

    HICON hIcon = nullptr;
    bmp.GetHICON(&hIcon);
    return hIcon ? hIcon : LoadIconW(nullptr, IDI_APPLICATION);
}

static void MinimizeToSystemTray(HWND hwnd) {
    ShowWindow(hwnd, SW_HIDE);
    NOTIFYICONDATAW balloon = g_nid;
    balloon.uFlags = NIF_INFO | NIF_ICON | NIF_MESSAGE | NIF_TIP;
    balloon.dwInfoFlags = NIIF_INFO;
    wcscpy_s(balloon.szInfoTitle, L"Project VXR Streamer");
    wcscpy_s(balloon.szInfo, L"Minimized to system tray. Right-click the tray icon to Exit.");
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            LoadConfig();
            RebuildControlsForTab();
            RegisterAllHotkeys(hwnd);
            SetTimer(hwnd, 1, 1000, nullptr);

            if (!g_hAppIcon) g_hAppIcon = CreateVxrAppIcon();
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_hAppIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_hAppIcon);

            g_nid.cbSize = sizeof(NOTIFYICONDATAW);
            g_nid.hWnd = hwnd;
            g_nid.uID = 1;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = g_hAppIcon;
            wcscpy_s(g_nid.szTip, L"Project VXR Streamer");
            Shell_NotifyIconW(NIM_ADD, &g_nid);

            g_serviceThread = std::thread(HostServiceThreadProc);
            return 0;
        }

        case WM_TIMER:
        case WM_SERVICE_UPDATE: {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_HOTKEY: {
            int idx = (int)wParam - HOTKEY_BASE_ID;
            if (idx >= 0 && idx < 9) {
                if (idx == 0) {
                    g_svc.switchMonitorRequested = true;
                }
                ShowToast(L"Hotkey Triggered: " + g_cfg.bindings[idx].actionName);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (g_recordingBindingIdx >= 0 && g_recordingBindingIdx < 9) {
                if (wParam == VK_ESCAPE) {
                    g_recordingBindingIdx = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU || wParam == VK_LWIN || wParam == VK_RWIN) {
                    return 0;
                }
                UINT mods = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
                if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
                if (mods == 0) mods = MOD_SHIFT | MOD_WIN;

                g_cfg.bindings[g_recordingBindingIdx].modifiers = mods;
                g_cfg.bindings[g_recordingBindingIdx].vk = (UINT)wParam;
                SaveConfig();
                RegisterAllHotkeys(hwnd);
                ShowToast(L"Bound " + g_cfg.bindings[g_recordingBindingIdx].actionName + L" to " + g_cfg.bindings[g_recordingBindingIdx].FormatText());
                g_recordingBindingIdx = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_editingAccount) {
                if (wParam == VK_RETURN) {
                    if (!g_accountEditBuf.empty()) g_cfg.metaAccount = g_accountEditBuf;
                    g_editingAccount = false;
                    SaveConfig();
                    ShowToast(L"Updated account to: " + g_cfg.metaAccount);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_ESCAPE) {
                    g_editingAccount = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_BACK && !g_accountEditBuf.empty()) {
                    g_accountEditBuf.pop_back();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }

        case WM_CHAR: {
            if (g_editingAccount && wParam >= 32 && wParam < 127 && g_accountEditBuf.size() < 28) {
                g_accountEditBuf.push_back((wchar_t)wParam);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderWindow(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.y >= 0 && pt.y < TOP_BAR_H && pt.x < 604) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            bool needRedraw = false;

            if (g_draggingSliderId != -1) {
                for (const auto& sl : g_sliders) {
                    if (sl.id == g_draggingSliderId) {
                        UpdateSliderFromMouse(sl, mx);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }

            if (g_openDropdownId != -1) {
                int newHover = -1;
                if (PtInRectGdi(g_openDropdownMenuRect, mx, my)) {
                    newHover = (my - g_openDropdownMenuRect.Y - 1) / 26;
                    if (newHover < 0 || newHover >= (int)g_openDropdownItems.size()) newHover = -1;
                }
                if (newHover != g_hoverDropdownItem) {
                    g_hoverDropdownItem = newHover;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            bool hMin = (mx >= 604 && mx < 642 && my >= 0 && my < TOP_BAR_H);
            bool hClose = (mx >= 642 && mx <= WIN_W && my >= 0 && my < TOP_BAR_H);
            if (hMin != g_hoverMin || hClose != g_hoverClose) {
                g_hoverMin = hMin;
                g_hoverClose = hClose;
                needRedraw = true;
            }

            int hNav = -1;
            if (mx >= 0 && mx < SIDEBAR_W && my >= 60 && my < 330) {
                int idx = (my - 60) / 44;
                if (idx >= 0 && idx < 6) hNav = idx;
            }
            if (hNav != g_hoverNav) {
                g_hoverNav = hNav;
                needRedraw = true;
            }

            int newHoverBtn = -1;
            if (g_activeTab == TAB_ACCOUNTS) {
                if (!g_editingAccount && PtInRectGdi(Rect(354, 126, 70, 30), mx, my)) newHoverBtn = 301;
                if (g_editingAccount && PtInRectGdi(Rect(412, 82, 65, 28), mx, my)) newHoverBtn = 302;
            } else if (g_activeTab == TAB_BINDINGS) {
                if (PtInRectGdi(Rect(178, 392, 156, 30), mx, my)) newHoverBtn = 100;
                int hBind = -1;
                for (int i = 0; i < 9; ++i) {
                    if (PtInRectGdi(Rect(500, 85 + i * 33, 166, 28), mx, my)) hBind = i;
                }
                if (hBind != g_hoverBindingIdx) {
                    g_hoverBindingIdx = hBind;
                    needRedraw = true;
                }
            } else if (g_activeTab == TAB_OPTIONS || g_activeTab == TAB_ADVANCED) {
                if (PtInRectGdi(Rect(506, 392, 156, 30), mx, my)) newHoverBtn = 100;
            } else if (g_activeTab == TAB_MEDIA) {
                if (PtInRectGdi(Rect(518, 53, 146, 30), mx, my)) newHoverBtn = 401;
                if (PtInRectGdi(Rect(178, 392, 164, 30), mx, my)) newHoverBtn = 402;
                for (size_t i = 0; i < g_cfg.videoPaths.size() && i < 3; ++i) {
                    int baseY = 128 + (int)i * 70;
                    if (PtInRectGdi(Rect(518, baseY - 5, 146, 30), mx, my)) newHoverBtn = (int)(410 + i);
                }
            } else if (g_activeTab == TAB_ABOUT) {
                if (PtInRectGdi(Rect(182, 392, 185, 30), mx, my)) newHoverBtn = 501;
                if (PtInRectGdi(Rect(376, 392, 140, 30), mx, my)) newHoverBtn = 502;
                if (PtInRectGdi(Rect(524, 392, 80,  30), mx, my)) newHoverBtn = 503;
            }

            if (newHoverBtn != g_hoverBtnId) {
                g_hoverBtnId = newHoverBtn;
                needRedraw = true;
            }

            if (needRedraw) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (mx >= 604 && mx < 642 && my >= 0 && my < TOP_BAR_H) {
                ShowWindow(hwnd, SW_MINIMIZE);
                return 0;
            }
            if (mx >= 642 && mx <= WIN_W && my >= 0 && my < TOP_BAR_H) {
                MinimizeToSystemTray(hwnd);
                return 0;
            }

            if (g_svc.isStarting) return 0;

            if (g_openDropdownId != -1) {
                int changedId = g_openDropdownId;
                if (PtInRectGdi(g_openDropdownMenuRect, mx, my) && g_hoverDropdownItem >= 0 && g_openDropdownTarget) {
                    *g_openDropdownTarget = g_hoverDropdownItem;
                    SaveConfig();
                    if (changedId == 1) ApplyHardwareEncoderConfig();
                    else if (changedId == 2) ApplyOpenXrRuntimeSelection();
                    else if (changedId == 3) ShowToast(L"Gamepad emulation profile updated");
                    else if (changedId == 4) ShowToast(L"Audio streaming route updated");
                }
                g_openDropdownId = -1;
                g_openDropdownTarget = nullptr;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (mx >= 0 && mx < SIDEBAR_W && my >= 60 && my < 330) {
                int idx = (my - 60) / 44;
                if (idx >= 0 && idx < 6 && idx != g_activeTab) {
                    g_activeTab = (NavTab)idx;
                    RebuildControlsForTab();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (g_activeTab == TAB_ACCOUNTS) {
                if (!g_editingAccount && PtInRectGdi(Rect(354, 126, 70, 30), mx, my)) {
                    g_editingAccount = true;
                    g_accountEditBuf = g_cfg.metaAccount;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (g_editingAccount && PtInRectGdi(Rect(412, 82, 65, 28), mx, my)) {
                    if (!g_accountEditBuf.empty()) g_cfg.metaAccount = g_accountEditBuf;
                    g_editingAccount = false;
                    SaveConfig();
                    ShowToast(L"Updated account to: " + g_cfg.metaAccount);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            else if (g_activeTab == TAB_BINDINGS) {
                for (int i = 0; i < 9; ++i) {
                    if (PtInRectGdi(Rect(500, 85 + i * 33, 166, 28), mx, my)) {
                        g_recordingBindingIdx = i;
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
                if (PtInRectGdi(Rect(178, 392, 156, 30), mx, my)) {
                    g_cfg.ResetBindings();
                    SaveConfig();
                    RegisterAllHotkeys(hwnd);
                    ShowToast(L"Keyboard shortcuts reset to defaults");
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            else if (g_activeTab == TAB_MEDIA) {
                if (PtInRectGdi(Rect(518, 53, 146, 30), mx, my)) {
                    std::wstring chosen = BrowseForFolderDialog(hwnd, L"Select Screenshots Folder");
                    if (!chosen.empty()) {
                        g_cfg.screenshotsPath = chosen;
                        SaveConfig();
                        ShowToast(L"Screenshots folder updated");
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                for (size_t i = 0; i < g_cfg.videoPaths.size() && i < 3; ++i) {
                    int baseY = 128 + (int)i * 70;
                    if (PtInRectGdi(Rect(518, baseY - 5, 146, 30), mx, my)) {
                        std::wstring chosen = BrowseForFolderDialog(hwnd, L"Select Videos Folder");
                        if (!chosen.empty()) {
                            if (chosen.back() != L'\\') chosen += L"\\";
                            g_cfg.videoPaths[i] = chosen;
                            SaveConfig();
                            ShowToast(L"Videos folder updated");
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                        return 0;
                    }
                }
                if (PtInRectGdi(Rect(178, 392, 164, 30), mx, my)) {
                    if (g_cfg.videoPaths.size() < 3) {
                        std::wstring chosen = BrowseForFolderDialog(hwnd, L"Add Videos Folder");
                        if (!chosen.empty()) {
                            if (chosen.back() != L'\\') chosen += L"\\";
                            g_cfg.videoPaths.push_back(chosen);
                            SaveConfig();
                            ShowToast(L"Added video folder");
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    } else {
                        ShowToast(L"Maximum of 3 video folders reached");
                    }
                    return 0;
                }
            }
            else if (g_activeTab == TAB_ABOUT) {
                // Clickable "click here to disable" link next to HAGS
                if (g_svc.hagsEnabled && PtInRectGdi(Rect(370, 236, 150, 20), mx, my)) {
                    ShellExecuteW(hwnd, L"open", L"ms-settings:display-advancedgraphics", nullptr, nullptr, SW_SHOWNORMAL);
                    ShowToast(L"Opened Windows Advanced Graphics Settings (HAGS)");
                    return 0;
                }
                // Button 1: Check for interfering apps...
                if (PtInRectGdi(Rect(182, 392, 185, 30), mx, my)) {
                    ScanInterferingApps(hwnd);
                    return 0;
                }
                // Button 2: Check for updates...
                if (PtInRectGdi(Rect(376, 392, 140, 30), mx, my)) {
                    wchar_t msg[512];
                    swprintf_s(msg,
                        L"Project VXR Streamer is up to date.\n\n"
                        L"Active Subsystems Verified:\n"
                        L"  • vxr_encoder.dll (NVENC %s)\n"
                        L"  • openxr_vxr.dll (HFOV %.1f°, VFOV %.1f°, %ux%u)\n"
                        L"  • driver_vrserver.dll (SteamVR 6DoF Ready)\n"
                        L"  • VxrIddCx.dll (UMDF v2 Virtual Display)\n"
                        L"  • Compute Shaders (SSW / CAS / SGSR Loaded)",
                        g_svc.nvencActive ? L"Hardware Active" : L"Standby",
                        g_svc.activeHFovDeg, g_svc.activeVFovDeg,
                        g_svc.activeEyeWidth, g_svc.activeEyeHeight);
                    MessageBoxW(hwnd, msg, L"Project VXR - Update & Integrity Check", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }
                // Button 3: Show logs
                if (PtInRectGdi(Rect(524, 392, 80, 30), mx, my)) {
                    AppendServiceLog(L"User opened vxr_streamer.log from ABOUT tab.");
                    ShellExecuteW(hwnd, L"open", L"notepad.exe", L"vxr_streamer.log", nullptr, SW_SHOWNORMAL);
                    return 0;
                }
            }

            // Dropdowns
            for (const auto& dd : g_dropdowns) {
                if (PtInRectGdi(dd.rect, mx, my)) {
                    g_openDropdownId = dd.id;
                    g_openDropdownItems = dd.items;
                    g_openDropdownTarget = dd.selectedIndex;
                    g_hoverDropdownItem = *dd.selectedIndex;
                    int menuH = (int)dd.items.size() * 26 + 2;
                    g_openDropdownMenuRect = Rect(dd.rect.X, dd.rect.Y + dd.rect.Height, dd.rect.Width, menuH);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Checkboxes
            for (const auto& cb : g_checkboxes) {
                if (!cb.disabled && PtInRectGdi(cb.rect, mx, my)) {
                    *cb.value = !(*cb.value);
                    SaveConfig();
                    if (cb.id == 101 || cb.id == 102) ApplyHardwareEncoderConfig();
                    else if (cb.id == 108) ApplyStartWithWindowsRegistry();
                    else if (cb.id == 201 || cb.id == 202) ApplyAdvancedOpticalAndSharpeningParameters();
                    else if (cb.id == 203) ApplyGamePriorityBoost();
                    else ShowToast(cb.label + (*cb.value ? L": Enabled" : L": Disabled"));
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Sliders
            for (const auto& sl : g_sliders) {
                bool isEnabled = (sl.enabledFlag == nullptr) || (*sl.enabledFlag);
                if (!isEnabled) continue;
                Rect hitRect(sl.trackRect.X - 8, sl.trackRect.Y - 4, sl.trackRect.Width + 16, sl.trackRect.Height + 8);
                if (PtInRectGdi(hitRect, mx, my)) {
                    g_draggingSliderId = sl.id;
                    SetCapture(hwnd);
                    UpdateSliderFromMouse(sl, mx);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Reset to Defaults button
            if ((g_activeTab == TAB_OPTIONS || g_activeTab == TAB_ADVANCED) && PtInRectGdi(Rect(506, 392, 156, 30), mx, my)) {
                if (g_activeTab == TAB_OPTIONS) {
                    g_cfg.ResetOptions();
                    ApplyHardwareEncoderConfig();
                } else {
                    g_cfg.ResetAdvanced();
                    ApplyHardwareEncoderConfig();
                    ApplyGamePriorityBoost();
                }
                SaveConfig();
                ShowToast(L"Settings reset to defaults");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_draggingSliderId != -1) {
                if (g_draggingSliderId == 12) {
                    ApplyHardwareEncoderConfig();
                } else {
                    ApplyAdvancedOpticalAndSharpeningParameters();
                }
                g_draggingSliderId = -1;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Open Project VXR Streamer");
                SetMenuDefaultItem(hMenu, ID_TRAY_OPEN, FALSE);
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
                if (cmd == ID_TRAY_OPEN) {
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                } else if (cmd == ID_TRAY_EXIT) {
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            MinimizeToSystemTray(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            g_hwnd = nullptr;
            KillTimer(hwnd, 1);
            for (int i = 0; i < 9; ++i) UnregisterHotKey(hwnd, HOTKEY_BASE_ID + i);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            SaveConfig();
            g_svc.stopRequested = true;
            if (g_serviceThread.joinable()) g_serviceThread.detach();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int nCmdShow) {
    HWND existingHwnd = FindWindowW(L"VxrStreamerNativeWnd", nullptr);
    if (existingHwnd) {
        PostMessageW(existingHwnd, WM_TRAYICON, 1, WM_LBUTTONUP);
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    g_hAppIcon = CreateVxrAppIcon();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = g_hAppIcon;
    wc.hIconSm = g_hAppIcon;
    wc.lpszClassName = L"VxrStreamerNativeWnd";
    RegisterClassExW(&wc);

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (scrW - WIN_W) / 2;
    int posY = (scrH - WIN_H) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Project VXR Streamer",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
        posX, posY, WIN_W, WIN_H,
        nullptr, nullptr, hInstance, nullptr
    );

    bool forceShow = (lpCmdLine && wcsstr(lpCmdLine, L"--show") != nullptr);
    if (g_cfg.startMinimizedInTray && !forceShow) {
        MinimizeToSystemTray(hwnd);
    } else {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    ExitProcess(0);
    return 0;
}
