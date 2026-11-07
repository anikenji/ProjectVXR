use crate::congestion::{CongestionConfig, CongestionController};
use crate::error::{PacerError, Result};
use crate::timer::HybridSleeper;
use crate::token_bucket::TokenBucket;
use std::collections::VecDeque;
use std::net::SocketAddr;
use vxr_protocol::{FeedbackPayload, Packet, VxrSocket, HEADER_SIZE};

#[derive(Debug, Clone)]
pub struct PacerConfig {
    pub max_queue_packets: usize,
    pub burst_capacity_bytes: usize,
    pub congestion: CongestionConfig,
    /// Explicit target microsecond sleep interval between video packets (e.g. 40-60 µs)
    pub fixed_micro_sleep_us: Option<u64>,
}

impl Default for PacerConfig {
    fn default() -> Self {
        Self {
            max_queue_packets: 4096,
            burst_capacity_bytes: 128 * 1024, // 128 KB
            congestion: CongestionConfig::default(),
            fixed_micro_sleep_us: Some(50), // 50 microseconds default
        }
    }
}

struct QueuedPacket {
    packet: Packet,
    dest: SocketAddr,
}

/// Token/Leaky Bucket Micro-Packet Pacer
/// Interleaves packets with 40-60 microsecond intervals to eliminate router queue bloat
/// while prioritizing low-latency audio, input, and control frames.
pub struct PacketPacer {
    config: PacerConfig,
    token_bucket: TokenBucket,
    congestion_controller: CongestionController,
    high_priority_queue: VecDeque<QueuedPacket>,
    video_queue: VecDeque<QueuedPacket>,
    total_packets_sent: u64,
    total_bytes_sent: u64,
}

impl PacketPacer {
    pub fn new(config: PacerConfig) -> Self {
        let initial_bitrate = config.congestion.start_bitrate_bps;
        let token_bucket = TokenBucket::new(initial_bitrate, config.burst_capacity_bytes);
        let congestion_controller = CongestionController::new(config.congestion.clone());

        Self {
            config,
            token_bucket,
            congestion_controller,
            high_priority_queue: VecDeque::with_capacity(128),
            video_queue: VecDeque::with_capacity(2048),
            total_packets_sent: 0,
            total_bytes_sent: 0,
        }
    }

    /// Enqueues high-priority traffic (Audio, Input, Control) to bypass video pacing
    pub fn enqueue_high_priority(&mut self, packet: Packet, dest: SocketAddr) -> Result<()> {
        if self.high_priority_queue.len() >= self.config.max_queue_packets {
            return Err(PacerError::QueueFull {
                current: self.high_priority_queue.len(),
                max: self.config.max_queue_packets,
            });
        }
        self.high_priority_queue.push_back(QueuedPacket { packet, dest });
        Ok(())
    }

    /// Enqueues video frame fragments & FEC parity shards for micro-packet pacing
    pub fn enqueue_video_packet(&mut self, packet: Packet, dest: SocketAddr) -> Result<()> {
        if self.video_queue.len() >= self.config.max_queue_packets {
            return Err(PacerError::QueueFull {
                current: self.video_queue.len(),
                max: self.config.max_queue_packets,
            });
        }
        self.video_queue.push_back(QueuedPacket { packet, dest });
        Ok(())
    }

    /// Enqueues an entire sliced video frame with all fragments
    pub fn enqueue_video_frame(&mut self, packets: Vec<Packet>, dest: SocketAddr) -> Result<()> {
        if self.video_queue.len() + packets.len() > self.config.max_queue_packets {
            return Err(PacerError::QueueFull {
                current: self.video_queue.len() + packets.len(),
                max: self.config.max_queue_packets,
            });
        }
        for packet in packets {
            self.video_queue.push_back(QueuedPacket { packet, dest });
        }
        Ok(())
    }

    /// Processes client feedback (RTT, packet loss) and updates dynamic bitrate
    pub fn update_feedback(&mut self, feedback: &FeedbackPayload) {
        self.congestion_controller.process_feedback(feedback);
        let new_bitrate = self.congestion_controller.current_bitrate_bps();
        self.token_bucket.set_bitrate(new_bitrate);
    }

    /// Transmits the next ready packet through the UDP socket:
    /// - High-priority packets are dispatched immediately without pacing.
    /// - Video packets are regulated via TokenBucket and paced with 40-60µs micro-sleeps.
    pub fn send_next(&mut self, socket: &VxrSocket) -> Result<Option<usize>> {
        // 1. Drain high-priority packets first
        if let Some(qp) = self.high_priority_queue.pop_front() {
            let bytes_len = qp.packet.payload.len() + HEADER_SIZE;
            let sent = socket.send_packet(&qp.packet, &qp.dest)?;
            self.total_packets_sent += 1;
            self.total_bytes_sent += bytes_len as u64;
            return Ok(Some(sent));
        }

        // 2. Pace video packet
        if let Some(qp) = self.video_queue.pop_front() {
            let bytes_len = qp.packet.payload.len() + HEADER_SIZE;

            // Apply token bucket rate limiter
            if let Err(wait_duration) = self.token_bucket.try_consume(bytes_len) {
                HybridSleeper::sleep_micros(wait_duration.as_micros() as u64);
            }

            // Apply micro-packet pacing inter-packet delay (40-60 µs)
            let micro_sleep_us = self.config.fixed_micro_sleep_us.unwrap_or_else(|| {
                self.congestion_controller.recommended_pacing_delay_us(bytes_len)
            });
            if micro_sleep_us > 0 {
                HybridSleeper::sleep_micros(micro_sleep_us);
            }

            let sent = socket.send_packet(&qp.packet, &qp.dest)?;
            self.total_packets_sent += 1;
            self.total_bytes_sent += bytes_len as u64;
            return Ok(Some(sent));
        }

        Ok(None)
    }

    /// Flushes all currently queued packets through the socket
    pub fn flush_all(&mut self, socket: &VxrSocket) -> Result<usize> {
        let mut count = 0;
        while self.has_pending_packets() {
            if self.send_next(socket)?.is_some() {
                count += 1;
            }
        }
        Ok(count)
    }

    pub fn has_pending_packets(&self) -> bool {
        !self.high_priority_queue.is_empty() || !self.video_queue.is_empty()
    }

    pub fn pending_high_priority_count(&self) -> usize {
        self.high_priority_queue.len()
    }

    pub fn pending_video_count(&self) -> usize {
        self.video_queue.len()
    }

    pub fn current_bitrate_bps(&self) -> u64 {
        self.congestion_controller.current_bitrate_bps()
    }

    pub fn total_packets_sent(&self) -> u64 {
        self.total_packets_sent
    }

    pub fn total_bytes_sent(&self) -> u64 {
        self.total_bytes_sent
    }
}
