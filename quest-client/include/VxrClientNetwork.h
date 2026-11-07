#ifndef VXR_CLIENT_NETWORK_H
#define VXR_CLIENT_NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace vxr {

struct VrMenuSettings;

static constexpr uint32_t VXR_UDP_VIDEO_MAGIC = 0x56585256; // "VXRV"
static constexpr uint32_t VXR_UDP_CTRL_MAGIC  = 0x56585243; // "VXRC"
static constexpr uint32_t VXR_HOST_INFO_MAGIC = 0x56585248; // "VXRH"
static constexpr size_t   VXR_UDP_FRAGMENT_STRIDE = 1200;

#pragma pack(push, 1)
struct VxrUdpVideoFragmentHeader {
    uint32_t magic;          // 0x56585256 ("VXRV")
    uint32_t frame_id;
    uint16_t frag_idx;
    uint16_t frag_count;
    uint8_t  flags;          // bit 0 = keyframe, bit 1 = intra_refresh
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
    uint8_t  cmd_type;       // 1=Discover/Connect, 2=MouseInput, 3=ClientSettings, 4=LaunchSteamVR, 5=SwitchMonitor
    uint8_t  reserved[3];
};

struct VxrControlDiscoverPacket {
    VxrControlHeader hdr;
    char headset_name[64];
};

struct VxrClientControlPacket {
    uint32_t magic;                // 0x56585243 ("VXRC")
    uint8_t  cmd_type;             // 1=Connect, 2=MouseInput, 3=StreamingSettings, 4=LaunchSteamVR, 5=SwitchMonitor
    uint8_t  mouse_button_mask;
    int16_t  mouse_wheel_delta;
    float    mouse_norm_x;
    float    mouse_norm_y;
    uint32_t desktop_bitrate_mbps;
    uint32_t vr_bitrate_mbps;
    uint32_t vr_quality_tier;
    uint32_t vr_fps;
    uint32_t sharpening_pct;
    uint8_t  ssw_mode;
    uint8_t  enable_sgsr;
    uint8_t  reserved[2];
};

struct VxrHostInfoWirePacket {
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

struct ReassembledVideoFrame {
    std::vector<uint8_t> data;
    size_t   size;
    uint32_t frame_id;
    uint64_t pts_us;
    bool     is_keyframe;
    bool     is_intra_refresh;
    uint8_t  codec_id;
    float    cursor_norm_x;
    float    cursor_norm_y;
    bool     cursor_visible;
};

struct DiscoveredHostInfo {
    std::string computer_name;
    std::string ip_address;
    std::string meta_account;
    std::string gpu_name;
    std::string codec_name;
    uint16_t    port;
    uint32_t    ping_ms;
};

/// Cross-platform UDP Video Receiver, Multi-Fragment Frame Reassembler & Control Client
class VxrClientNetwork {
public:
    VxrClientNetwork();
    ~VxrClientNetwork();

    /// Initializes non-blocking UDP sockets on streamPort (:38810), beaconPort (:38811), and mdnsPort (:5353)
    bool Initialize(uint16_t streamPort = 38810, uint16_t mdnsPort = 5353);
    void Cleanup();
    void Shutdown() { Cleanup(); }

    /// Queries the exact local Wi-Fi IPv4 address via routing table lookup (connect to 8.8.8.8:53 + getsockname)
    /// and computes the /24 subnet broadcast address (e.g. 192.168.1.255).
    std::string QueryLocalWifiIp();
    const std::string& GetLocalWifiIp() const { return m_localWifiIp; }
    const std::string& GetSubnetBroadcastIp() const { return m_subnetBroadcastIp; }

    /// Multi-Fragment Frame Reassembly: collects incoming UDP fragments for frame_id,
    /// copies each fragment payload into offset frag_idx * 1200, and outputs a complete
    /// ReassembledVideoFrame once all frag_count fragments arrive.
    bool ReassembleFrame(const uint8_t* packetData, size_t packetLen, ReassembledVideoFrame* outFrame);

    /// Polls m_udpSock (:38810), m_beaconSock (:38811), and m_mdnsSock (:5353) for VXR_HOST_INFO_MAGIC,
    /// VXR_MDNS_BEACON, and incoming video fragments.
    bool PollNetwork(ReassembledVideoFrame* outFrame);

    /// Polls the non-blocking mDNS socket (:5353) and beacon socket (:38811) for host discovery beacons
    bool PollMdnsDiscovery(std::vector<DiscoveredHostInfo>* outHosts = nullptr);

    /// Parses an mDNS beacon payload string into a DiscoveredHostInfo entry
    bool ParseMdnsBeacon(const char* beaconStr, size_t len, const char* senderIp, DiscoveredHostInfo* outHost);

    /// Broadcasts VxrControlDiscoverPacket (magic=0x56585243, cmd_type=1, headset_name) to both
    /// the computed subnet broadcast (e.g. 192.168.1.255:38810) and 255.255.255.255:38810
    /// WITHOUT setting m_connected = true.
    bool SendBroadcastDiscovery(const char* headsetModelName, uint16_t port = 38810);

    // Outbound Control Packets (magic 0x56585243 "VXRC")
    bool SendDiscoverAndConnect(const char* hostIp, uint16_t port = 38810, const char* headsetModelName = "Meta Quest");
    void Disconnect();
    bool SendDesktopMouseInput(float normX, float normY, uint8_t buttonMask, int16_t wheelDelta = 0);
    bool SendClientStreamingSettings(uint32_t desktopBitrateMbps,
                                     uint32_t vrBitrateMbps,
                                     uint32_t vrQualityTier,
                                     uint32_t vrFps,
                                     uint32_t sharpeningPct,
                                     uint8_t sswMode,
                                     bool enableSgsr);
    bool SendClientSettings(const VrMenuSettings& settings);
    bool SendLaunchSteamVr();
    bool SendSwitchMonitor();

    bool IsInitialized() const { return m_initialized; }
    bool IsConnected() const { return m_connected; }
    bool IsConnectPending() const { return m_userRequestedConnect; }
    const std::string& GetConnectedHostIp() const { return m_connectedHostIp; }
    uint16_t GetConnectedHostPort() const { return m_connectedHostPort; }
    uint64_t GetPacketsReceived() const { return m_packetsReceived; }
    uint64_t GetFramesReassembled() const { return m_framesReassembled; }
    uint64_t GetControlPacketsSent() const { return m_controlPacketsSent; }
    float GetMeasuredRttMs() const { return m_measuredRttMs; }
    const std::vector<DiscoveredHostInfo>& GetDiscoveredHosts() const { return m_discoveredHosts; }
    const VxrClientControlPacket& GetLastSentControlPacket() const { return m_lastControlPacket; }

private:
    struct PendingFrameSlot {
        uint32_t frame_id;
        uint16_t frag_count;
        uint16_t received_count;
        uint8_t  flags;
        uint8_t  codec_id;
        uint64_t pts_us;
        float    cursor_norm_x;
        float    cursor_norm_y;
        uint8_t  cursor_visible;
        size_t   exact_size;
        size_t   max_end_offset;
        std::vector<bool>    received_mask;
        std::vector<uint8_t> buffer;
    };

    bool SendControlPacketInternal(const VxrClientControlPacket& pkt);
    bool HandleIncomingControlOrBeaconPacket(const uint8_t* rxBuf, size_t len, const char* senderIp);
    void RecordHostArrivalTiming();

    bool m_initialized;
    bool m_connected;
    bool m_userRequestedConnect;
    uint16_t m_streamPort;
    uint16_t m_beaconPort;
    uint16_t m_mdnsPort;
    std::string m_connectedHostIp;
    uint16_t m_connectedHostPort;
    std::string m_localWifiIp;
    std::string m_subnetBroadcastIp;

    uintptr_t m_udpSock;
    uintptr_t m_beaconSock;
    uintptr_t m_mdnsSock;

    uint64_t m_packetsReceived;
    uint64_t m_framesReassembled;
    uint64_t m_controlPacketsSent;
    uint64_t m_lastOutboundTimestampUs;
    uint64_t m_lastInboundTimestampUs;
    float    m_measuredRttMs;

    std::unordered_map<uint32_t, PendingFrameSlot> m_pendingFrames;
    std::vector<uint32_t> m_pendingFrameOrder;
    std::vector<DiscoveredHostInfo> m_discoveredHosts;
    VxrClientControlPacket m_lastControlPacket;
};

} // namespace vxr

#endif // VXR_CLIENT_NETWORK_H
