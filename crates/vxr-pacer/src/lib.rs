//! # VXR Pacer
//!
//! Micro-packet pacing engine, anti-bufferbloat rate limiter,
//! and dynamic bitrate / congestion control loop for Project VXR.

pub mod congestion;
pub mod error;
pub mod pacer;
pub mod timer;
pub mod token_bucket;

pub use congestion::{CongestionConfig, CongestionController, NetworkState};
pub use error::{PacerError, Result};
pub use pacer::{PacerConfig, PacketPacer};
pub use timer::HybridSleeper;
pub use token_bucket::TokenBucket;

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Instant;
    use vxr_protocol::{flags, FeedbackPayload, Packet, PacketType, SocketConfig, VxrSocket};

    #[test]
    fn test_token_bucket_rate_limiting() {
        // 80 Mbps = 10 MB/s, capacity 20KB
        let mut tb = TokenBucket::new(80_000_000, 20_000);

        // Can consume within capacity immediately
        assert!(tb.try_consume(15_000).is_ok());

        // Consuming beyond available capacity returns required wait duration
        let res = tb.try_consume(10_000);
        assert!(res.is_err());
        let wait = res.unwrap_err();
        assert!(wait.as_micros() > 0);

        // Update rate to 200 Mbps
        tb.set_bitrate(200_000_000);
        assert_eq!(tb.current_bitrate_bps(), 200_000_000);
    }

    #[test]
    fn test_hybrid_sleeper_accuracy() {
        // Measure 50 microsecond sleep
        let start = Instant::now();
        HybridSleeper::sleep_micros(50);
        let elapsed = start.elapsed();

        // Must sleep at least 50us, and reasonably close (not 15ms like standard Windows sleep!)
        assert!(elapsed.as_micros() >= 45);
        assert!(elapsed.as_micros() < 2_000, "HybridSleeper must avoid coarse OS 15ms quantum");
    }

    #[test]
    fn test_congestion_controller_aimd_and_backoff() {
        let config = CongestionConfig {
            min_bitrate_bps: 30_000_000,
            max_bitrate_bps: 200_000_000,
            start_bitrate_bps: 100_000_000,
            loss_threshold: 0.005,
            max_queue_delay_ms: 6.0,
            additive_increase_bps: 5_000_000,
            multiplicative_decrease_factor: 0.85,
        };
        let mut cc = CongestionController::new(config);

        assert_eq!(cc.current_bitrate_bps(), 100_000_000);
        assert_eq!(cc.state(), NetworkState::Normal);

        // 1. Clean link: RTT=5ms, Loss=0 -> Bitrate increases additively
        cc.process_metrics(5_000, 0, 0.0);
        assert_eq!(cc.current_bitrate_bps(), 105_000_000);
        assert_eq!(cc.state(), NetworkState::Normal);

        // 2. High loss (1.5% > 0.5% threshold) -> Multiplicative backoff triggers
        cc.process_metrics(5_500, 15, 0.015);
        assert_eq!(cc.state(), NetworkState::Congested);
        assert!(cc.current_bitrate_bps() < 105_000_000);
        assert_eq!(cc.total_backoffs(), 1);

        // 3. High queuing delay (RTT jumps from 5ms to 18ms -> queue delay 13ms > 6ms) -> Backoff triggers
        cc.process_metrics(18_000, 0, 0.0);
        assert_eq!(cc.state(), NetworkState::Congested);
        assert_eq!(cc.total_backoffs(), 2);

        // 4. Verify pacing delay calculation
        let delay_us = cc.recommended_pacing_delay_us(1350);
        assert!(delay_us >= 20 && delay_us <= 100);
    }

    #[test]
    fn test_packet_pacer_priority_and_udp_transmission() {
        let socket_config = SocketConfig {
            non_blocking: false,
            ..Default::default()
        };

        let server_socket = VxrSocket::bind("127.0.0.1:0".parse().unwrap(), socket_config.clone()).unwrap();
        let server_addr = server_socket.local_addr().unwrap();

        let client_socket = VxrSocket::bind("127.0.0.1:0".parse().unwrap(), socket_config).unwrap();

        let pacer_config = PacerConfig {
            fixed_micro_sleep_us: Some(10), // 10us for fast unit tests
            ..Default::default()
        };
        let mut pacer = PacketPacer::new(pacer_config);

        // Enqueue 3 video packets
        for i in 0..3 {
            let p = Packet::new(PacketType::Video, i, flags::NONE, 1000 + i as u64, vec![1, 2, 3]).unwrap();
            pacer.enqueue_video_packet(p, server_addr).unwrap();
        }

        // Enqueue 1 high-priority Audio packet (should jump ahead of remaining video packets!)
        let audio_packet = Packet::new(PacketType::Audio, 999, flags::RELIABLE, 55555, vec![9, 9, 9]).unwrap();
        pacer.enqueue_high_priority(audio_packet, server_addr).unwrap();

        assert_eq!(pacer.pending_video_count(), 3);
        assert_eq!(pacer.pending_high_priority_count(), 1);

        // Send first packet -> Must be the high-priority Audio packet!
        let sent1 = pacer.send_next(&client_socket).unwrap();
        assert!(sent1.is_some());
        assert_eq!(pacer.pending_high_priority_count(), 0);
        assert_eq!(pacer.pending_video_count(), 3);

        // Read packet on server side to verify it was indeed Audio
        let mut buf = [0u8; 1024];
        let (recv_pkt, _) = server_socket.recv_packet(&mut buf).unwrap();
        assert_eq!(recv_pkt.header.packet_type, PacketType::Audio);
        assert_eq!(recv_pkt.header.sequence, 999);

        // Flush remaining video packets
        let flushed = pacer.flush_all(&client_socket).unwrap();
        assert_eq!(flushed, 3);
        assert!(!pacer.has_pending_packets());
        assert_eq!(pacer.total_packets_sent(), 4);

        // Update feedback
        let fb = FeedbackPayload {
            last_frame_received: 10,
            frames_lost: 0,
            packets_lost: 0,
            packet_loss_rate: 0.0,
            rtt_us: 4000,
            suggested_bitrate_bps: 120_000_000,
            battery_level: 95,
        };
        pacer.update_feedback(&fb);
        assert!(pacer.current_bitrate_bps() > 100_000_000);
    }
}
