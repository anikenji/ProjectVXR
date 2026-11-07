#include "VxrClientNetwork.h"
#include "VxrVrMenuUi.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET VxrRawSocket;
static constexpr VxrRawSocket VXR_INVALID_SOCK = INVALID_SOCKET;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int VxrRawSocket;
static constexpr VxrRawSocket VXR_INVALID_SOCK = static_cast<VxrRawSocket>(-1);
#endif

namespace vxr {

static uint64_t GetSteadyClockUs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

static void ClosePlatformSocket(uintptr_t& sockHandle) {
    VxrRawSocket s = static_cast<VxrRawSocket>(sockHandle);
    if (s != VXR_INVALID_SOCK) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        sockHandle = static_cast<uintptr_t>(VXR_INVALID_SOCK);
    }
}

static void SetSocketNonBlocking(VxrRawSocket s) {
#ifdef _WIN32
    u_long nonBlock = 1;
    ioctlsocket(s, FIONBIO, &nonBlock);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
    }
#endif
}

VxrClientNetwork::VxrClientNetwork()
    : m_initialized(false)
    , m_connected(false)
    , m_userRequestedConnect(false)
    , m_streamPort(38810)
    , m_beaconPort(38811)
    , m_mdnsPort(5353)
    , m_connectedHostIp("")
    , m_connectedHostPort(38810)
    , m_localWifiIp("0.0.0.0")
    , m_subnetBroadcastIp("255.255.255.255")
    , m_udpSock(static_cast<uintptr_t>(VXR_INVALID_SOCK))
    , m_beaconSock(static_cast<uintptr_t>(VXR_INVALID_SOCK))
    , m_mdnsSock(static_cast<uintptr_t>(VXR_INVALID_SOCK))
    , m_packetsReceived(0)
    , m_framesReassembled(0)
    , m_controlPacketsSent(0)
    , m_lastOutboundTimestampUs(0)
    , m_lastInboundTimestampUs(0)
    , m_measuredRttMs(0.0f)
{
    std::memset(&m_lastControlPacket, 0, sizeof(m_lastControlPacket));
}

VxrClientNetwork::~VxrClientNetwork() {
    Cleanup();
}

std::string VxrClientNetwork::QueryLocalWifiIp() {
    VxrRawSocket tmpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tmpSock == VXR_INVALID_SOCK) {
        return m_localWifiIp;
    }

    sockaddr_in remoteAddr = {};
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remoteAddr.sin_addr);

    if (connect(tmpSock, reinterpret_cast<const sockaddr*>(&remoteAddr), sizeof(remoteAddr)) == 0) {
        sockaddr_in localAddr = {};
#ifdef _WIN32
        int addrLen = sizeof(localAddr);
#else
        socklen_t addrLen = sizeof(localAddr);
#endif
        if (getsockname(tmpSock, reinterpret_cast<sockaddr*>(&localAddr), &addrLen) == 0) {
            char ipBuf[64] = {};
            if (inet_ntop(AF_INET, &localAddr.sin_addr, ipBuf, sizeof(ipBuf)) != nullptr && ipBuf[0] != '\0') {
                m_localWifiIp = ipBuf;
                // Compute /24 subnet broadcast address (e.g. "192.168.1.15" -> "192.168.1.255")
                size_t lastDot = m_localWifiIp.rfind('.');
                if (lastDot != std::string::npos && m_localWifiIp != "127.0.0.1" && m_localWifiIp != "0.0.0.0") {
                    m_subnetBroadcastIp = m_localWifiIp.substr(0, lastDot + 1) + "255";
                }
            }
        }
    }

#ifdef _WIN32
    closesocket(tmpSock);
#else
    close(tmpSock);
#endif
    return m_localWifiIp;
}

bool VxrClientNetwork::Initialize(uint16_t streamPort, uint16_t mdnsPort) {
    Cleanup();
    m_streamPort = streamPort;
    m_beaconPort = 38811;
    m_mdnsPort = mdnsPort;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif

    // Detect real local Wi-Fi IPv4 and /24 subnet broadcast IP
    QueryLocalWifiIp();

    // 1. Create non-blocking UDP streaming & control socket (:38810)
    VxrRawSocket udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSock != VXR_INVALID_SOCK) {
        int reuse = 1;
        setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        int bcast = 1;
        setsockopt(udpSock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bcast), sizeof(bcast));
        int rcvBuf = 8 * 1024 * 1024;
        setsockopt(udpSock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
        SetSocketNonBlocking(udpSock);

        sockaddr_in bindAddr = {};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(m_streamPort);
        bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(udpSock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
            bindAddr.sin_port = htons(0);
            bind(udpSock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr));
        }
        m_udpSock = static_cast<uintptr_t>(udpSock);
    }

    // 2. Create non-blocking UDP broadcast beacon discovery listener (:38811)
    VxrRawSocket beaconSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (beaconSock != VXR_INVALID_SOCK) {
        int reuse = 1;
        setsockopt(beaconSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(beaconSock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#endif
        int bcast = 1;
        setsockopt(beaconSock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bcast), sizeof(bcast));
        SetSocketNonBlocking(beaconSock);

        sockaddr_in beaconBind = {};
        beaconBind.sin_family = AF_INET;
        beaconBind.sin_port = htons(m_beaconPort);
        beaconBind.sin_addr.s_addr = htonl(INADDR_ANY);
        bind(beaconSock, reinterpret_cast<sockaddr*>(&beaconBind), sizeof(beaconBind));
        m_beaconSock = static_cast<uintptr_t>(beaconSock);
    }

    // 3. Create non-blocking UDP mDNS discovery listener (:5353)
    VxrRawSocket mdnsSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mdnsSock != VXR_INVALID_SOCK) {
        int reuse = 1;
        setsockopt(mdnsSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(mdnsSock, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#endif
        int bcast = 1;
        setsockopt(mdnsSock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bcast), sizeof(bcast));
        SetSocketNonBlocking(mdnsSock);

        sockaddr_in mdnsBind = {};
        mdnsBind.sin_family = AF_INET;
        mdnsBind.sin_port = htons(m_mdnsPort);
        mdnsBind.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(mdnsSock, reinterpret_cast<sockaddr*>(&mdnsBind), sizeof(mdnsBind)) == 0) {
            ip_mreq mreq = {};
            inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            setsockopt(mdnsSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq));
        }
        m_mdnsSock = static_cast<uintptr_t>(mdnsSock);
    }

    m_initialized = (m_udpSock != static_cast<uintptr_t>(VXR_INVALID_SOCK));
    m_connected = false;
    m_userRequestedConnect = false;
    m_packetsReceived = 0;
    m_framesReassembled = 0;
    m_controlPacketsSent = 0;
    m_pendingFrames.clear();
    m_pendingFrameOrder.clear();

    std::cout << "[VXR-Net] Initialized Quest UDP Network Client (localIp=" << m_localWifiIp
              << ", subnetBcast=" << m_subnetBroadcastIp
              << ", streamPort=:" << m_streamPort
              << ", beaconPort=:" << m_beaconPort
              << ", mdnsPort=:" << m_mdnsPort << ")" << std::endl;
    return m_initialized;
}

void VxrClientNetwork::Cleanup() {
    ClosePlatformSocket(m_udpSock);
    ClosePlatformSocket(m_beaconSock);
    ClosePlatformSocket(m_mdnsSock);
    m_pendingFrames.clear();
    m_pendingFrameOrder.clear();
    m_connected = false;
    m_userRequestedConnect = false;
    if (m_initialized) {
#ifdef _WIN32
        WSACleanup();
#endif
        m_initialized = false;
    }
}

void VxrClientNetwork::Disconnect() {
    m_connected = false;
    m_userRequestedConnect = false;
    m_connectedHostIp.clear();
}

void VxrClientNetwork::RecordHostArrivalTiming() {
    uint64_t nowUs = GetSteadyClockUs();
    if (m_lastOutboundTimestampUs > 0 && nowUs >= m_lastOutboundTimestampUs) {
        float rttMs = static_cast<float>(nowUs - m_lastOutboundTimestampUs) / 1000.0f;
        if (rttMs > 0.05f && rttMs < 500.0f) {
            m_measuredRttMs = (m_measuredRttMs <= 0.01f) ? rttMs : (m_measuredRttMs * 0.8f + rttMs * 0.2f);
            m_lastOutboundTimestampUs = 0;
        }
    } else if (m_lastInboundTimestampUs > 0 && nowUs > m_lastInboundTimestampUs) {
        float deltaMs = static_cast<float>(nowUs - m_lastInboundTimestampUs) / 1000.0f;
        if (deltaMs > 0.1f && deltaMs < 100.0f && m_measuredRttMs <= 0.01f) {
            m_measuredRttMs = deltaMs;
        }
    }
    m_lastInboundTimestampUs = nowUs;
}

bool VxrClientNetwork::ReassembleFrame(const uint8_t* packetData, size_t packetLen, ReassembledVideoFrame* outFrame) {
    if (!packetData || packetLen == 0 || !outFrame) {
        return false;
    }

    m_packetsReceived++;

    if (packetLen >= sizeof(VxrUdpVideoFragmentHeader)) {
        const VxrUdpVideoFragmentHeader* hdr = reinterpret_cast<const VxrUdpVideoFragmentHeader*>(packetData);
        if (hdr->magic == VXR_UDP_VIDEO_MAGIC) {
            if (hdr->frag_count == 0 || hdr->frag_idx >= hdr->frag_count) {
                return false;
            }
            size_t headerSize = sizeof(VxrUdpVideoFragmentHeader);
            if (headerSize + hdr->payload_len > packetLen) {
                return false;
            }

            const uint8_t* payloadPtr = packetData + headerSize;
            uint32_t frameId = hdr->frame_id;

            // Fast-path for single-fragment frames
            if (hdr->frag_count == 1) {
                outFrame->data.assign(payloadPtr, payloadPtr + hdr->payload_len);
                outFrame->size = outFrame->data.size();
                outFrame->frame_id = frameId;
                outFrame->pts_us = hdr->pts_us;
                outFrame->is_keyframe = (hdr->flags & 0x01) != 0;
                outFrame->is_intra_refresh = (hdr->flags & 0x02) != 0;
                outFrame->codec_id = hdr->codec_id;
                outFrame->cursor_norm_x = hdr->cursor_norm_x;
                outFrame->cursor_norm_y = hdr->cursor_norm_y;
                outFrame->cursor_visible = (hdr->cursor_visible != 0);
                m_framesReassembled++;
                return true;
            }

            // Multi-fragment frame reassembly
            auto it = m_pendingFrames.find(frameId);
            if (it == m_pendingFrames.end()) {
                while (m_pendingFrameOrder.size() >= 64) {
                    uint32_t oldestId = m_pendingFrameOrder.front();
                    m_pendingFrameOrder.erase(m_pendingFrameOrder.begin());
                    m_pendingFrames.erase(oldestId);
                }

                PendingFrameSlot slot;
                slot.frame_id = frameId;
                slot.frag_count = hdr->frag_count;
                slot.received_count = 0;
                slot.flags = hdr->flags;
                slot.codec_id = hdr->codec_id;
                slot.pts_us = hdr->pts_us;
                slot.cursor_norm_x = hdr->cursor_norm_x;
                slot.cursor_norm_y = hdr->cursor_norm_y;
                slot.cursor_visible = hdr->cursor_visible;
                slot.exact_size = 0;
                slot.max_end_offset = 0;
                slot.received_mask.assign(hdr->frag_count, false);
                slot.buffer.assign(static_cast<size_t>(hdr->frag_count) * VXR_UDP_FRAGMENT_STRIDE, 0);

                m_pendingFrameOrder.push_back(frameId);
                it = m_pendingFrames.emplace(frameId, std::move(slot)).first;
            }

            PendingFrameSlot& slot = it->second;
            if (hdr->frag_idx >= slot.frag_count) {
                return false;
            }

            size_t fragOffset = static_cast<size_t>(hdr->frag_idx) * VXR_UDP_FRAGMENT_STRIDE;
            size_t endOffset = fragOffset + hdr->payload_len;
            if (endOffset > slot.buffer.size()) {
                slot.buffer.resize(endOffset, 0);
            }

            if (hdr->payload_len > 0) {
                std::memcpy(slot.buffer.data() + fragOffset, payloadPtr, hdr->payload_len);
            }

            if (endOffset > slot.max_end_offset) {
                slot.max_end_offset = endOffset;
            }
            if (hdr->frag_idx == hdr->frag_count - 1) {
                slot.exact_size = endOffset;
            }

            slot.flags |= hdr->flags;
            slot.pts_us = hdr->pts_us;
            slot.cursor_norm_x = hdr->cursor_norm_x;
            slot.cursor_norm_y = hdr->cursor_norm_y;
            slot.cursor_visible = hdr->cursor_visible;

            if (!slot.received_mask[hdr->frag_idx]) {
                slot.received_mask[hdr->frag_idx] = true;
                slot.received_count++;
            }

            if (slot.received_count == slot.frag_count) {
                size_t finalSize = (slot.exact_size > 0) ? slot.exact_size : slot.max_end_offset;
                slot.buffer.resize(finalSize);

                outFrame->data = std::move(slot.buffer);
                outFrame->size = outFrame->data.size();
                outFrame->frame_id = slot.frame_id;
                outFrame->pts_us = slot.pts_us;
                outFrame->is_keyframe = (slot.flags & 0x01) != 0;
                outFrame->is_intra_refresh = (slot.flags & 0x02) != 0;
                outFrame->codec_id = slot.codec_id;
                outFrame->cursor_norm_x = slot.cursor_norm_x;
                outFrame->cursor_norm_y = slot.cursor_norm_y;
                outFrame->cursor_visible = (slot.cursor_visible != 0);

                m_pendingFrames.erase(it);
                m_pendingFrameOrder.erase(
                    std::remove(m_pendingFrameOrder.begin(), m_pendingFrameOrder.end(), frameId),
                    m_pendingFrameOrder.end());

                m_framesReassembled++;
                return true;
            }

            return false;
        }
    }

    // Legacy single-packet Annex-B bitstream fallback (00 00 00 01)
    if (packetLen >= 4 && packetData[0] == 0x00 && packetData[1] == 0x00 &&
        (packetData[2] == 0x01 || (packetData[2] == 0x00 && packetData[3] == 0x01))) {
        outFrame->data.assign(packetData, packetData + packetLen);
        outFrame->size = packetLen;
        outFrame->frame_id = static_cast<uint32_t>(m_framesReassembled + 1);
        outFrame->pts_us = 0;
        outFrame->is_keyframe = true;
        outFrame->is_intra_refresh = false;
        outFrame->codec_id = 1;
        outFrame->cursor_norm_x = 0.5f;
        outFrame->cursor_norm_y = 0.5f;
        outFrame->cursor_visible = true;
        m_framesReassembled++;
        return true;
    }

    return false;
}

bool VxrClientNetwork::HandleIncomingControlOrBeaconPacket(const uint8_t* rxBuf, size_t len, const char* senderIp) {
    if (!rxBuf || len == 0) {
        return false;
    }

    // 1. Check for binary VXR_HOST_INFO_MAGIC (0x56585248 "VXRH")
    if (len >= 4) {
        uint32_t magic = 0;
        std::memcpy(&magic, rxBuf, 4);
        if (magic == VXR_HOST_INFO_MAGIC) {
            uint8_t isHandshakeAck = (len >= 16) ? rxBuf[15] : 0; // wire->reserved == 1 means unicast handshake ACK from PC
            if (isHandshakeAck == 1) {
                RecordHostArrivalTiming();
            }

            DiscoveredHostInfo hInfo = {};
            hInfo.ip_address = (senderIp && senderIp[0] != '\0') ? senderIp : "127.0.0.1";
            hInfo.computer_name = "VXR-PC";
            hInfo.port = 38810;
            hInfo.ping_ms = (m_measuredRttMs >= 1.0f && m_measuredRttMs < 50.0f) ? static_cast<uint32_t>(m_measuredRttMs) : 3;

            if (len >= 80) {
                uint16_t hostPort = 0;
                std::memcpy(&hostPort, rxBuf + 4, sizeof(uint16_t));
                if (hostPort > 0) {
                    hInfo.port = hostPort;
                }
                char compName[65] = {};
                std::memcpy(compName, rxBuf + 16, 64);
                if (compName[0] != '\0') {
                    hInfo.computer_name = compName;
                }
            }
            if (len >= sizeof(VxrHostInfoWirePacket)) {
                const VxrHostInfoWirePacket* wire = reinterpret_cast<const VxrHostInfoWirePacket*>(rxBuf);
                char gpuBuf[65] = {};
                char codecBuf[33] = {};
                std::memcpy(gpuBuf, wire->gpu_name, 64);
                std::memcpy(codecBuf, wire->codec_name, 32);
                hInfo.gpu_name = gpuBuf;
                hInfo.codec_name = codecBuf;
            }

            bool found = false;
            for (auto& ex : m_discoveredHosts) {
                if (ex.ip_address == hInfo.ip_address) {
                    ex = hInfo;
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_discoveredHosts.push_back(hInfo);
            }

            // CRITICAL: Only transition m_connected = true when the PC sends a unicast Handshake ACK (isHandshakeAck == 1),
            // NEVER on a periodic 1-way broadcast beacon (isHandshakeAck == 0)!
            if (isHandshakeAck == 1 && m_userRequestedConnect && !m_connectedHostIp.empty() && hInfo.ip_address == m_connectedHostIp) {
                m_connected = true;
            }
            return true;
        }
    }

    // 2. Check for "VXR_ACK_OK" (always a direct unicast reply from PC)
    if (len >= 10 && std::memcmp(rxBuf, "VXR_ACK_OK", 10) == 0) {
        RecordHostArrivalTiming();
        if (m_userRequestedConnect) {
            m_connected = true;
            if (senderIp && senderIp[0] != '\0' && m_connectedHostIp.empty()) {
                m_connectedHostIp = senderIp;
            }
        }
        return true;
    }

    // 3. Check for text beacon "VXR_MDNS_BEACON:" (discovery only, do not pollute RTT timing)
    DiscoveredHostInfo beaconHost = {};
    if (ParseMdnsBeacon(reinterpret_cast<const char*>(rxBuf), len, senderIp, &beaconHost)) {
        return true;
    }

    return false;
}

bool VxrClientNetwork::PollNetwork(ReassembledVideoFrame* outFrame) {
    if (!m_initialized) {
        return false;
    }

    VxrRawSocket s = static_cast<VxrRawSocket>(m_udpSock);
    if (s == VXR_INVALID_SOCK) {
        return false;
    }

    uint8_t rxBuf[2048];
    for (int iter = 0; iter < 256; ++iter) {
        sockaddr_in fromAddr = {};
#ifdef _WIN32
        int fromLen = sizeof(fromAddr);
#else
        socklen_t fromLen = sizeof(fromAddr);
#endif
        int r = recvfrom(s, reinterpret_cast<char*>(rxBuf), sizeof(rxBuf), 0,
                         reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
        if (r <= 0) {
            break;
        }

        char senderIp[64] = {};
        inet_ntop(AF_INET, &fromAddr.sin_addr, senderIp, sizeof(senderIp));

        if (HandleIncomingControlOrBeaconPacket(rxBuf, static_cast<size_t>(r), senderIp)) {
            continue;
        }

        if (outFrame && ReassembleFrame(rxBuf, static_cast<size_t>(r), outFrame)) {
            RecordHostArrivalTiming();
            if (m_userRequestedConnect) {
                m_connected = true;
                if (senderIp[0] != '\0' && m_connectedHostIp.empty()) {
                    m_connectedHostIp = senderIp;
                }
            }
            return true;
        }
    }
    return false;
}

bool VxrClientNetwork::ParseMdnsBeacon(const char* beaconStr, size_t len, const char* senderIp, DiscoveredHostInfo* outHost) {
    if (!beaconStr || len == 0 || !outHost) {
        return false;
    }

    std::string msg(beaconStr, len);
    if (msg.find("VXR_MDNS_BEACON:") == std::string::npos) {
        return false;
    }

    outHost->ip_address = (senderIp && senderIp[0]) ? senderIp : "127.0.0.1";
    outHost->computer_name = "VXR-PC";
    outHost->meta_account = "";
    outHost->port = 38810;
    outHost->ping_ms = (m_measuredRttMs >= 1.0f) ? static_cast<uint32_t>(m_measuredRttMs) : 2;

    size_t userPos = msg.find("user=");
    if (userPos != std::string::npos) {
        size_t start = userPos + 5;
        size_t end = msg.find(';', start);
        outHost->meta_account = (end != std::string::npos) ? msg.substr(start, end - start) : msg.substr(start);
        if (!outHost->meta_account.empty()) {
            outHost->computer_name = outHost->meta_account + "-PC";
        }
    }

    size_t hostPos = msg.find("host=");
    if (hostPos != std::string::npos) {
        size_t start = hostPos + 5;
        size_t end = msg.find(';', start);
        outHost->computer_name = (end != std::string::npos) ? msg.substr(start, end - start) : msg.substr(start);
    }

    size_t portPos = msg.find("port=");
    if (portPos != std::string::npos) {
        size_t start = portPos + 5;
        size_t end = msg.find(';', start);
        std::string portStr = (end != std::string::npos) ? msg.substr(start, end - start) : msg.substr(start);
        int p = std::atoi(portStr.c_str());
        if (p > 0 && p <= 65535) {
            outHost->port = static_cast<uint16_t>(p);
        }
    }

    bool updated = false;
    for (auto& existing : m_discoveredHosts) {
        if (existing.ip_address == outHost->ip_address) {
            existing.computer_name = outHost->computer_name;
            existing.meta_account = outHost->meta_account;
            existing.port = outHost->port;
            existing.ping_ms = outHost->ping_ms;
            updated = true;
            break;
        }
    }
    if (!updated) {
        m_discoveredHosts.push_back(*outHost);
    }
    return true;
}

bool VxrClientNetwork::PollMdnsDiscovery(std::vector<DiscoveredHostInfo>* outHosts) {
    if (!m_initialized) {
        return false;
    }

    bool foundNew = false;
    uintptr_t socks[2] = { m_mdnsSock, m_beaconSock };

    for (int sIdx = 0; sIdx < 2; ++sIdx) {
        VxrRawSocket s = static_cast<VxrRawSocket>(socks[sIdx]);
        if (s == VXR_INVALID_SOCK) continue;

        uint8_t rxBuf[1024];
        for (int iter = 0; iter < 16; ++iter) {
            sockaddr_in fromAddr = {};
#ifdef _WIN32
            int fromLen = sizeof(fromAddr);
#else
            socklen_t fromLen = sizeof(fromAddr);
#endif
            int r = recvfrom(s, reinterpret_cast<char*>(rxBuf), sizeof(rxBuf) - 1, 0,
                             reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);
            if (r <= 0) {
                break;
            }
            rxBuf[r] = 0;

            char ipStr[64] = {};
            inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, sizeof(ipStr));

            if (HandleIncomingControlOrBeaconPacket(rxBuf, static_cast<size_t>(r), ipStr)) {
                foundNew = true;
            }
        }
    }

    if (outHosts) {
        *outHosts = m_discoveredHosts;
    }
    return foundNew;
}

bool VxrClientNetwork::SendBroadcastDiscovery(const char* headsetModelName, uint16_t port) {
    if (m_localWifiIp == "0.0.0.0" || m_localWifiIp.empty()) {
        QueryLocalWifiIp();
    }

    VxrControlDiscoverPacket discPkt = {};
    discPkt.hdr.magic = VXR_UDP_CTRL_MAGIC; // 0x56585243 ("VXRC")
    discPkt.hdr.cmd_type = 1;               // VXR_CMD_DISCOVER
    const char* model = (headsetModelName && headsetModelName[0] != '\0') ? headsetModelName : "Meta Quest";
    std::strncpy(discPkt.headset_name, model, sizeof(discPkt.headset_name) - 1);

    m_controlPacketsSent++;
    m_lastOutboundTimestampUs = GetSteadyClockUs();

    VxrRawSocket s = static_cast<VxrRawSocket>(m_udpSock);
    if (!m_initialized || s == VXR_INVALID_SOCK) {
        return false;
    }

    uint16_t targetPort = (port > 0) ? port : m_streamPort;

    // 1. Send to computed /24 subnet broadcast (e.g. 192.168.1.255:38810)
    if (!m_subnetBroadcastIp.empty() && m_subnetBroadcastIp != "255.255.255.255") {
        sockaddr_in subAddr = {};
        subAddr.sin_family = AF_INET;
        subAddr.sin_port = htons(targetPort);
        if (inet_pton(AF_INET, m_subnetBroadcastIp.c_str(), &subAddr.sin_addr) == 1) {
            sendto(s, reinterpret_cast<const char*>(&discPkt), sizeof(discPkt), 0,
                   reinterpret_cast<const sockaddr*>(&subAddr), sizeof(subAddr));
        }
    }

    // 2. Send to global limited broadcast (255.255.255.255:38810)
    sockaddr_in bcastAddr = {};
    bcastAddr.sin_family = AF_INET;
    bcastAddr.sin_port = htons(targetPort);
    inet_pton(AF_INET, "255.255.255.255", &bcastAddr.sin_addr);
    int sent = sendto(s, reinterpret_cast<const char*>(&discPkt), sizeof(discPkt), 0,
                      reinterpret_cast<const sockaddr*>(&bcastAddr), sizeof(bcastAddr));

    // IMPORTANT: Never set m_connected = true on broadcast discovery!
    return (sent == static_cast<int>(sizeof(discPkt)));
}

bool VxrClientNetwork::SendControlPacketInternal(const VxrClientControlPacket& pkt) {
    m_lastControlPacket = pkt;
    m_controlPacketsSent++;
    m_lastOutboundTimestampUs = GetSteadyClockUs();

    VxrRawSocket s = static_cast<VxrRawSocket>(m_udpSock);
    if (!m_initialized || s == VXR_INVALID_SOCK || m_connectedHostIp.empty()) {
        return true;
    }

    sockaddr_in destAddr = {};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_connectedHostPort);
    if (inet_pton(AF_INET, m_connectedHostIp.c_str(), &destAddr.sin_addr) != 1) {
        return false;
    }

    int sent = sendto(s, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
                      reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
    return (sent == static_cast<int>(sizeof(pkt)));
}

bool VxrClientNetwork::SendDiscoverAndConnect(const char* hostIp, uint16_t port, const char* headsetModelName) {
    if (!hostIp || hostIp[0] == '\0') {
        return false;
    }

    bool isBroadcast = (std::strcmp(hostIp, "255.255.255.255") == 0) ||
                       (m_subnetBroadcastIp == hostIp);
    if (isBroadcast) {
        return SendBroadcastDiscovery(headsetModelName, port);
    }

    m_connectedHostIp = hostIp;
    if (port > 0) {
        m_connectedHostPort = port;
    }
    m_userRequestedConnect = true;
    m_lastOutboundTimestampUs = GetSteadyClockUs();

    // Send 72-byte VxrControlDiscoverPacket with the real headset model name to the unicast PC IP
    VxrControlDiscoverPacket discPkt = {};
    discPkt.hdr.magic = VXR_UDP_CTRL_MAGIC; // 0x56585243 ("VXRC")
    discPkt.hdr.cmd_type = 1;               // VXR_CMD_CONNECT
    const char* model = (headsetModelName && headsetModelName[0] != '\0') ? headsetModelName : "Meta Quest";
    std::strncpy(discPkt.headset_name, model, sizeof(discPkt.headset_name) - 1);

    m_lastControlPacket.magic = VXR_UDP_CTRL_MAGIC;
    m_lastControlPacket.cmd_type = 1;
    m_controlPacketsSent++;

    VxrRawSocket s = static_cast<VxrRawSocket>(m_udpSock);
    if (!m_initialized || s == VXR_INVALID_SOCK) {
        return false;
    }

    sockaddr_in destAddr = {};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_connectedHostPort);
    if (inet_pton(AF_INET, m_connectedHostIp.c_str(), &destAddr.sin_addr) != 1) {
        return false;
    }

    int sent = sendto(s, reinterpret_cast<const char*>(&discPkt), sizeof(discPkt), 0,
                      reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
    return (sent == static_cast<int>(sizeof(discPkt)));
}

bool VxrClientNetwork::SendDesktopMouseInput(float normX, float normY, uint8_t buttonMask, int16_t wheelDelta) {
    VxrClientControlPacket pkt = {};
    pkt.magic = VXR_UDP_CTRL_MAGIC; // 0x56585243 ("VXRC")
    pkt.cmd_type = 2;
    pkt.mouse_button_mask = buttonMask;
    pkt.mouse_wheel_delta = wheelDelta;
    pkt.mouse_norm_x = (std::max)(0.0f, (std::min)(1.0f, normX));
    pkt.mouse_norm_y = (std::max)(0.0f, (std::min)(1.0f, normY));
    return SendControlPacketInternal(pkt);
}

bool VxrClientNetwork::SendClientStreamingSettings(uint32_t desktopBitrateMbps,
                                                   uint32_t vrBitrateMbps,
                                                   uint32_t vrQualityTier,
                                                   uint32_t vrFps,
                                                   uint32_t sharpeningPct,
                                                   uint8_t sswMode,
                                                   bool enableSgsr) {
    VxrClientControlPacket pkt = {};
    pkt.magic = VXR_UDP_CTRL_MAGIC; // 0x56585243 ("VXRC")
    pkt.cmd_type = 3;
    pkt.desktop_bitrate_mbps = desktopBitrateMbps;
    pkt.vr_bitrate_mbps = vrBitrateMbps;
    pkt.vr_quality_tier = vrQualityTier;
    pkt.vr_fps = vrFps;
    pkt.sharpening_pct = sharpeningPct;
    pkt.ssw_mode = sswMode;
    pkt.enable_sgsr = enableSgsr ? 1 : 0;
    return SendControlPacketInternal(pkt);
}

bool VxrClientNetwork::SendClientSettings(const VrMenuSettings& settings) {
    return SendClientStreamingSettings(
        settings.desktop_bitrate_mbps,
        settings.vr_bitrate_mbps,
        settings.vr_graphics_quality,
        settings.vr_frame_rate,
        settings.sharpening_pct,
        static_cast<uint8_t>(settings.ssw_mode),
        settings.snapdragon_sgsr
    );
}

bool VxrClientNetwork::SendLaunchSteamVr() {
    VxrClientControlPacket pkt = {};
    pkt.magic = VXR_UDP_CTRL_MAGIC; // 0x56585243 ("VXRC")
    pkt.cmd_type = 4;
    return SendControlPacketInternal(pkt);
}

bool VxrClientNetwork::SendSwitchMonitor() {
    VxrClientControlPacket pkt = {};
    pkt.magic = VXR_UDP_CTRL_MAGIC; // 0x56585243 ("VXRC")
    pkt.cmd_type = 5;
    return SendControlPacketInternal(pkt);
}

bool VxrClientNetwork::SendVrTrackingState(const VxrControlTrackingPacket& trackingPkt) {
    VxrRawSocket s = static_cast<VxrRawSocket>(m_udpSock);
    if (!m_initialized || s == VXR_INVALID_SOCK || m_connectedHostIp.empty()) {
        return false;
    }

    sockaddr_in destAddr = {};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(m_connectedHostPort);
    if (inet_pton(AF_INET, m_connectedHostIp.c_str(), &destAddr.sin_addr) != 1) {
        return false;
    }

    int sent = sendto(s, reinterpret_cast<const char*>(&trackingPkt), sizeof(trackingPkt), 0,
                      reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
    return (sent == static_cast<int>(sizeof(trackingPkt)));
}

} // namespace vxr
