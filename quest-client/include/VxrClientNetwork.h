#ifndef VXR_CLIENT_NETWORK_H
#define VXR_CLIENT_NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace vxr {

static constexpr uint32_t VXR_UDP_VIDEO_MAGIC = 0x56585256; // "VXRV"
static constexpr uint32_t VXR_UDP_CTRL_MAGIC  = 0x56585243; // "VXRC"
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
    uint16_t    port;
    uint32_t    ping_ms;
};

/// Cross-platform UDP Video Receiver, Multi-Fragment Frame Reassembler & Control Client
class VxrClientNetwork {
public:
    VxrClientNetwork();
    ~VxrClientNetwork();

    /// Initializes non-blocking UDP sockets on streamPort (:38810) and mdnsPort (:5353)
    bool Initialize(uint16_t streamPort = 38810, uint16_t mdnsPort = 5353);
    void Cleanup();
    void Shutdown() { Cleanup(); }

    /// Multi-Fragment Frame Reassembly: collects incoming UDP fragments for frame_id,
    /// copies each fragment payload into offset frag_idx * 1200, and outputs a complete
    /// ReassembledVideoFrame once all frag_count fragments arrive.
    bool ReassembleFrame(const uint8_t* packetData, size_t packetLen, ReassembledVideoFrame* outFrame);

    /// Polls the non-blocking UDP streaming socket for incoming video fragments or ACKs
    bool PollNetwork(ReassembledVideoFrame* outFrame);

    /// Polls the non-blocking mDNS socket (:5353) for host discovery beacons
    bool PollMdnsDiscovery(std::vector<DiscoveredHostInfo>* outHosts = nullptr);

    /// Parses an mDNS beacon payload string into a DiscoveredHostInfo entry
    bool ParseMdnsBeacon(const char* beaconStr, size_t len, const char* senderIp, DiscoveredHostInfo* outHost);

    // Outbound Control Packets (magic 0x56585243 "VXRC")
    bool SendDiscoverAndConnect(const char* hostIp, uint16_t port = 38810);
    bool SendDesktopMouseInput(float normX, float normY, uint8_t buttonMask, int16_t wheelDelta = 0);
    bool SendClientStreamingSettings(uint32_t desktopBitrateMbps,
                                     uint32_t vrBitrateMbps,
                                     uint32_t vrQualityTier,
                                     uint32_t vrFps,
                                     uint32_t sharpeningPct,
                                     uint8_t sswMode,
                                     bool enableSgsr);
    bool SendLaunchSteamVr();
    bool SendSwitchMonitor();

    bool IsInitialized() const { return m_initialized; }
    bool IsConnected() const { return m_connected; }
    const std::string& GetConnectedHostIp() const { return m_connectedHostIp; }
    uint16_t GetConnectedHostPort() const { return m_connectedHostPort; }
    uint64_t GetPacketsReceived() const { return m_packetsReceived; }
    uint64_t GetFramesReassembled() const { return m_framesReassembled; }
    uint64_t GetControlPacketsSent() const { return m_controlPacketsSent; }
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

    bool m_initialized;
    bool m_connected;
    uint16_t m_streamPort;
    uint16_t m_mdnsPort;
    std::string m_connectedHostIp;
    uint16_t m_connectedHostPort;

    uintptr_t m_udpSocket;
    uintptr_t m_mdnsSocket;

    uint64_t m_packetsReceived;
    uint64_t m_framesReassembled;
    uint64_t m_controlPacketsSent;

    std::unordered_map<uint32_t, PendingFrameSlot> m_pendingFrames;
    std::vector<uint32_t> m_pendingFrameOrder;
    std::vector<DiscoveredHostInfo> m_discoveredHosts;
    VxrClientControlPacket m_lastControlPacket;
};

} // namespace vxr

#endif // VXR_CLIENT_NETWORK_H
