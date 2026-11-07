use socket2::{Domain, Protocol, Socket, Type};
use std::net::{SocketAddr, UdpSocket};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use crate::error::Result;
use crate::packet::Packet;

/// Default socket buffer sizes: 4MB for high-throughput 100-500 Mbps video streaming
pub const DEFAULT_SOCKET_BUFFER_SIZE: usize = 4 * 1024 * 1024;

/// DSCP 46 (Expedited Forwarding / DSCP_EF) -> IP_TOS = 46 << 2 = 184 (0xB8)
pub const DSCP_EXPEDITED_FORWARDING: u8 = 46;
pub const TOS_EXPEDITED_FORWARDING: u32 = (DSCP_EXPEDITED_FORWARDING as u32) << 2;

#[derive(Debug, Clone)]
pub struct SocketConfig {
    pub recv_buffer_size: usize,
    pub send_buffer_size: usize,
    pub dscp: Option<u8>,
    pub non_blocking: bool,
    pub reuse_address: bool,
}

impl Default for SocketConfig {
    fn default() -> Self {
        Self {
            recv_buffer_size: DEFAULT_SOCKET_BUFFER_SIZE,
            send_buffer_size: DEFAULT_SOCKET_BUFFER_SIZE,
            dscp: Some(DSCP_EXPEDITED_FORWARDING),
            non_blocking: true,
            reuse_address: true,
        }
    }
}

#[derive(Debug, Default)]
pub struct SocketStats {
    pub packets_sent: AtomicU64,
    pub packets_recv: AtomicU64,
    pub bytes_sent: AtomicU64,
    pub bytes_recv: AtomicU64,
}

#[derive(Clone)]
pub struct VxrSocket {
    raw: Arc<UdpSocket>,
    stats: Arc<SocketStats>,
}

impl VxrSocket {
    /// Binds a UDP socket with the specified configuration (buffer sizes, DSCP QoS, non-blocking)
    pub fn bind(addr: SocketAddr, config: SocketConfig) -> Result<Self> {
        let domain = match addr {
            SocketAddr::V4(_) => Domain::IPV4,
            SocketAddr::V6(_) => Domain::IPV6,
        };

        let socket = Socket::new(domain, Type::DGRAM, Some(Protocol::UDP))?;

        if config.reuse_address {
            let _ = socket.set_reuse_address(true);
        }

        // Configure receive and send buffer sizes
        if let Err(e) = socket.set_recv_buffer_size(config.recv_buffer_size) {
            log::warn!("Could not set socket recv buffer size to {}: {}", config.recv_buffer_size, e);
        }
        if let Err(e) = socket.set_send_buffer_size(config.send_buffer_size) {
            log::warn!("Could not set socket send buffer size to {}: {}", config.send_buffer_size, e);
        }

        // Configure DSCP / TOS for Wi-Fi QoS priority
        if let Some(dscp) = config.dscp {
            let tos = (dscp as u32) << 2;
            if addr.is_ipv4() {
                if let Err(e) = socket.set_tos(tos) {
                    log::warn!("Could not set IPv4 TOS/DSCP to {}: {}", tos, e);
                }
            }
        }

        socket.bind(&addr.into())?;

        if config.non_blocking {
            socket.set_nonblocking(true)?;
        }

        let udp_socket: UdpSocket = socket.into();
        Ok(Self {
            raw: Arc::new(udp_socket),
            stats: Arc::new(SocketStats::default()),
        })
    }

    /// Connects the UDP socket to a remote endpoint
    pub fn connect(&self, remote: SocketAddr) -> Result<()> {
        self.raw.connect(remote)?;
        Ok(())
    }

    /// Local socket address
    pub fn local_addr(&self) -> Result<SocketAddr> {
        Ok(self.raw.local_addr()?)
    }

    /// Sends a VXR packet to the specified remote address
    pub fn send_packet(&self, packet: &Packet, dest: &SocketAddr) -> Result<usize> {
        let bytes = packet.encode();
        let sent = self.raw.send_to(&bytes, dest)?;
        self.stats.packets_sent.fetch_add(1, Ordering::Relaxed);
        self.stats.bytes_sent.fetch_add(sent as u64, Ordering::Relaxed);
        Ok(sent)
    }

    /// Sends raw bytes to the specified remote address
    pub fn send_raw(&self, data: &[u8], dest: &SocketAddr) -> Result<usize> {
        let sent = self.raw.send_to(data, dest)?;
        self.stats.packets_sent.fetch_add(1, Ordering::Relaxed);
        self.stats.bytes_sent.fetch_add(sent as u64, Ordering::Relaxed);
        Ok(sent)
    }

    /// Receives and decodes a VXR packet into `buffer`
    pub fn recv_packet(&self, buffer: &mut [u8]) -> Result<(Packet, SocketAddr)> {
        let (len, src) = self.raw.recv_from(buffer)?;
        self.stats.packets_recv.fetch_add(1, Ordering::Relaxed);
        self.stats.bytes_recv.fetch_add(len as u64, Ordering::Relaxed);

        let packet = Packet::decode(&buffer[..len])?;
        Ok((packet, src))
    }

    /// Receives raw bytes from the socket
    pub fn recv_raw(&self, buffer: &mut [u8]) -> Result<(usize, SocketAddr)> {
        let (len, src) = self.raw.recv_from(buffer)?;
        self.stats.packets_recv.fetch_add(1, Ordering::Relaxed);
        self.stats.bytes_recv.fetch_add(len as u64, Ordering::Relaxed);
        Ok((len, src))
    }

    /// Sends a packet to the connected remote endpoint
    pub fn send_connected(&self, packet: &Packet) -> Result<usize> {
        let bytes = packet.encode();
        let sent = self.raw.send(&bytes)?;
        self.stats.packets_sent.fetch_add(1, Ordering::Relaxed);
        self.stats.bytes_sent.fetch_add(sent as u64, Ordering::Relaxed);
        Ok(sent)
    }

    /// Receives a packet from the connected remote endpoint
    pub fn recv_connected(&self, buffer: &mut [u8]) -> Result<Packet> {
        let len = self.raw.recv(buffer)?;
        self.stats.packets_recv.fetch_add(1, Ordering::Relaxed);
        self.stats.bytes_recv.fetch_add(len as u64, Ordering::Relaxed);

        let packet = Packet::decode(&buffer[..len])?;
        Ok(packet)
    }

    /// Returns a reference to transmission statistics
    pub fn stats(&self) -> &SocketStats {
        &self.stats
    }

    /// Underlying std UdpSocket clone
    pub fn inner(&self) -> Arc<UdpSocket> {
        self.raw.clone()
    }
}
