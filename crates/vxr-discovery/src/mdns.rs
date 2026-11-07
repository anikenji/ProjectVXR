use std::collections::HashMap;
use std::net::{Ipv4Addr, SocketAddrV4, UdpSocket};
use crate::error::{DiscoveryError, Result};

pub const MDNS_MULTICAST_IPV4: Ipv4Addr = Ipv4Addr::new(224, 0, 0, 251);
pub const MDNS_PORT: u16 = 5353;
pub const VXR_SERVICE_TYPE: &str = "_vxr._udp.local.";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MdnsServiceInfo {
    pub service_name: String,
    pub host_name: String,
    pub ip: Ipv4Addr,
    pub port: u16,
    pub txt_records: HashMap<String, String>,
}

impl MdnsServiceInfo {
    pub fn new(host_name: &str, ip: Ipv4Addr, port: u16) -> Self {
        let mut txt = HashMap::new();
        txt.insert("version".to_string(), "1.0.0".to_string());
        txt.insert("status".to_string(), "available".to_string());
        txt.insert("codecs".to_string(), "H264,HEVC,AV1".to_string());
        txt.insert("p2p".to_string(), "supported".to_string());

        Self {
            service_name: format!("{}.{}", host_name, VXR_SERVICE_TYPE),
            host_name: host_name.to_string(),
            ip,
            port,
            txt_records: txt,
        }
    }
}

/// Binary mDNS Packet Builder and Parser (RFC 1035 / RFC 6762)
pub struct MdnsPacket;

impl MdnsPacket {
    /// Builds an unsolicited mDNS announcement response packet containing PTR, SRV, A, and TXT records
    pub fn build_announcement(service: &MdnsServiceInfo) -> Vec<u8> {
        let mut buf = Vec::with_capacity(512);

        // Header (12 bytes): ID=0, Flags=0x8400 (Response, Authoritative), Questions=0, Answers=4
        buf.extend_from_slice(&[0x00, 0x00]); // Transaction ID
        buf.extend_from_slice(&[0x84, 0x00]); // Flags
        buf.extend_from_slice(&[0x00, 0x00]); // QDCOUNT (Questions)
        buf.extend_from_slice(&[0x00, 0x04]); // ANCOUNT (Answers: PTR, SRV, A, TXT)
        buf.extend_from_slice(&[0x00, 0x00]); // NSCOUNT
        buf.extend_from_slice(&[0x00, 0x00]); // ARCOUNT

        // 1. PTR Record: _vxr._udp.local. -> <host>._vxr._udp.local.
        Self::encode_domain_name(&mut buf, VXR_SERVICE_TYPE);
        buf.extend_from_slice(&[0x00, 0x0C]); // Type: PTR (12)
        buf.extend_from_slice(&[0x00, 0x01]); // Class: IN (1)
        buf.extend_from_slice(&[0x00, 0x00, 0x00, 0x78]); // TTL: 120s

        let ptr_target_start = buf.len();
        buf.extend_from_slice(&[0x00, 0x00]); // Placeholder for RDLENGTH
        let target_pos = buf.len();
        Self::encode_domain_name(&mut buf, &service.service_name);
        let ptr_len = (buf.len() - target_pos) as u16;
        buf[ptr_target_start..ptr_target_start + 2].copy_from_slice(&ptr_len.to_be_bytes());

        // 2. SRV Record: Port and Target
        Self::encode_domain_name(&mut buf, &service.service_name);
        buf.extend_from_slice(&[0x00, 0x21]); // Type: SRV (33)
        buf.extend_from_slice(&[0x00, 0x01]); // Class: IN
        buf.extend_from_slice(&[0x00, 0x00, 0x00, 0x78]); // TTL

        let srv_target_start = buf.len();
        buf.extend_from_slice(&[0x00, 0x00]); // Placeholder for RDLENGTH
        let srv_pos = buf.len();
        buf.extend_from_slice(&[0x00, 0x00]); // Priority: 0
        buf.extend_from_slice(&[0x00, 0x00]); // Weight: 0
        buf.extend_from_slice(&service.port.to_be_bytes()); // Port
        Self::encode_domain_name(&mut buf, &format!("{}.local.", service.host_name));
        let srv_len = (buf.len() - srv_pos) as u16;
        buf[srv_target_start..srv_target_start + 2].copy_from_slice(&srv_len.to_be_bytes());

        // 3. A Record: IPv4 address
        Self::encode_domain_name(&mut buf, &format!("{}.local.", service.host_name));
        buf.extend_from_slice(&[0x00, 0x01]); // Type: A (1)
        buf.extend_from_slice(&[0x00, 0x01]); // Class: IN
        buf.extend_from_slice(&[0x00, 0x00, 0x00, 0x78]); // TTL
        buf.extend_from_slice(&[0x00, 0x04]); // RDLENGTH: 4 bytes
        buf.extend_from_slice(&service.ip.octets());

        // 4. TXT Record: Attributes (version, status, codecs, p2p)
        Self::encode_domain_name(&mut buf, &service.service_name);
        buf.extend_from_slice(&[0x00, 0x10]); // Type: TXT (16)
        buf.extend_from_slice(&[0x00, 0x01]); // Class: IN
        buf.extend_from_slice(&[0x00, 0x00, 0x00, 0x78]); // TTL

        let txt_target_start = buf.len();
        buf.extend_from_slice(&[0x00, 0x00]); // Placeholder for RDLENGTH
        let txt_pos = buf.len();
        for (k, v) in &service.txt_records {
            let entry = format!("{}={}", k, v);
            let bytes = entry.as_bytes();
            if bytes.len() < 256 {
                buf.push(bytes.len() as u8);
                buf.extend_from_slice(bytes);
            }
        }
        let txt_len = (buf.len() - txt_pos) as u16;
        buf[txt_target_start..txt_target_start + 2].copy_from_slice(&txt_len.to_be_bytes());

        buf
    }

    /// Builds a standard mDNS PTR query packet to discover VXR services
    pub fn build_query() -> Vec<u8> {
        let mut buf = Vec::with_capacity(64);
        buf.extend_from_slice(&[0x00, 0x00]); // Transaction ID
        buf.extend_from_slice(&[0x00, 0x00]); // Flags (Query)
        buf.extend_from_slice(&[0x00, 0x01]); // QDCOUNT: 1 question
        buf.extend_from_slice(&[0x00, 0x00]); // ANCOUNT
        buf.extend_from_slice(&[0x00, 0x00]); // NSCOUNT
        buf.extend_from_slice(&[0x00, 0x00]); // ARCOUNT

        Self::encode_domain_name(&mut buf, VXR_SERVICE_TYPE);
        buf.extend_from_slice(&[0x00, 0x0C]); // Type: PTR
        buf.extend_from_slice(&[0x80, 0x01]); // Class: IN, unicast-response bit set
        buf
    }

    /// Parses an incoming mDNS packet to extract VXR service information
    pub fn parse_packet(packet: &[u8]) -> Option<MdnsServiceInfo> {
        if packet.len() < 12 {
            return None;
        }

        let flags = u16::from_be_bytes([packet[2], packet[3]]);
        let is_response = (flags & 0x8000) != 0;
        if !is_response {
            return None;
        }

        // Search for A record and SRV record
        let mut found_ip = None;
        let mut found_port = None;
        let mut txt_map = HashMap::new();

        // Scan for A record (type 0x0001) with 4-byte IPv4 payload
        for i in 12..packet.len().saturating_sub(10) {
            // Match pattern: Type A (0x0001), Class IN (0x0001), TTL (4 bytes), RDLEN (0x0004)
            if packet[i] == 0x00 && packet[i + 1] == 0x01
                && packet[i + 2] == 0x00 && packet[i + 3] == 0x01
                && packet[i + 8] == 0x00 && packet[i + 9] == 0x04
            {
                let ip_start = i + 10;
                if ip_start + 4 <= packet.len() {
                    found_ip = Some(Ipv4Addr::new(
                        packet[ip_start],
                        packet[ip_start + 1],
                        packet[ip_start + 2],
                        packet[ip_start + 3],
                    ));
                }
            }

            // Match pattern: Type SRV (0x0021)
            if packet[i] == 0x00 && packet[i + 1] == 0x21
                && packet[i + 2] == 0x00 && packet[i + 3] == 0x01
            {
                let port_start = i + 14;
                if port_start + 2 <= packet.len() {
                    found_port = Some(u16::from_be_bytes([packet[port_start], packet[port_start + 1]]));
                }
            }

            // Match pattern: Type TXT (0x0010)
            if packet[i] == 0x00 && packet[i + 1] == 0x10
                && packet[i + 2] == 0x00 && packet[i + 3] == 0x01
            {
                let rdlen = u16::from_be_bytes([packet[i + 8], packet[i + 9]]) as usize;
                let data_start = i + 10;
                if data_start + rdlen <= packet.len() {
                    let mut pos = data_start;
                    while pos < data_start + rdlen {
                        let str_len = packet[pos] as usize;
                        pos += 1;
                        if pos + str_len <= data_start + rdlen {
                            if let Ok(s) = std::str::from_utf8(&packet[pos..pos + str_len]) {
                                if let Some((k, v)) = s.split_once('=') {
                                    txt_map.insert(k.to_string(), v.to_string());
                                }
                            }
                            pos += str_len;
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        if let (Some(ip), Some(port)) = (found_ip, found_port) {
            Some(MdnsServiceInfo {
                service_name: format!("VXR-Host.{}", VXR_SERVICE_TYPE),
                host_name: "VXR-Host".to_string(),
                ip,
                port,
                txt_records: txt_map,
            })
        } else {
            None
        }
    }

    fn encode_domain_name(buf: &mut Vec<u8>, domain: &str) {
        for label in domain.trim_end_matches('.').split('.') {
            buf.push(label.len() as u8);
            buf.extend_from_slice(label.as_bytes());
        }
        buf.push(0); // Zero terminator
    }
}

/// mDNS Local Service Advertiser
pub struct MdnsAdvertiser {
    service: MdnsServiceInfo,
    socket: Option<UdpSocket>,
}

impl MdnsAdvertiser {
    pub fn new(service: MdnsServiceInfo) -> Self {
        Self {
            service,
            socket: None,
        }
    }

    /// Binds local UDP socket for mDNS multicasting
    pub fn start(&mut self) -> Result<()> {
        let socket = UdpSocket::bind("0.0.0.0:0")
            .map_err(|e| DiscoveryError::Mdns(format!("Failed to bind mDNS socket: {:?}", e)))?;
        let _ = socket.set_multicast_loop_v4(true);
        let _ = socket.set_broadcast(true);
        let _ = socket.set_nonblocking(true);
        self.socket = Some(socket);
        self.broadcast_announcement()
    }

    /// Multicasts an announcement packet to 224.0.0.251:5353
    pub fn broadcast_announcement(&self) -> Result<()> {
        if let Some(ref socket) = self.socket {
            let packet = MdnsPacket::build_announcement(&self.service);
            let dest = SocketAddrV4::new(MDNS_MULTICAST_IPV4, MDNS_PORT);
            let _ = socket.send_to(&packet, dest);
        }
        Ok(())
    }

    pub fn service(&self) -> &MdnsServiceInfo {
        &self.service
    }
}

/// mDNS Local Service Browser
pub struct MdnsBrowser {
    socket: Option<UdpSocket>,
}

impl Default for MdnsBrowser {
    fn default() -> Self {
        Self::new()
    }
}

impl MdnsBrowser {
    pub fn new() -> Self {
        Self { socket: None }
    }

    pub fn start(&mut self) -> Result<()> {
        let socket = UdpSocket::bind("0.0.0.0:0")
            .map_err(|e| DiscoveryError::Mdns(format!("Failed to bind mDNS browser socket: {:?}", e)))?;
        let _ = socket.set_broadcast(true);
        let _ = socket.set_nonblocking(true);
        self.socket = Some(socket);
        Ok(())
    }

    pub fn send_query(&self) -> Result<()> {
        if let Some(ref socket) = self.socket {
            let query = MdnsPacket::build_query();
            let dest = SocketAddrV4::new(MDNS_MULTICAST_IPV4, MDNS_PORT);
            let _ = socket.send_to(&query, dest);
        }
        Ok(())
    }

    pub fn poll_discovered(&self) -> Vec<MdnsServiceInfo> {
        let mut results = Vec::new();
        if let Some(ref socket) = self.socket {
            let mut buf = [0u8; 2048];
            while let Ok((len, _)) = socket.recv_from(&mut buf) {
                if let Some(service) = MdnsPacket::parse_packet(&buf[..len]) {
                    results.push(service);
                }
            }
        }
        results
    }
}
