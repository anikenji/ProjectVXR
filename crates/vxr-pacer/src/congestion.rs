use std::time::Instant;
use vxr_protocol::FeedbackPayload;

#[derive(Debug, Clone)]
pub struct CongestionConfig {
    pub min_bitrate_bps: u64,
    pub max_bitrate_bps: u64,
    pub start_bitrate_bps: u64,
    pub loss_threshold: f32,
    pub max_queue_delay_ms: f32,
    pub additive_increase_bps: u64,
    pub multiplicative_decrease_factor: f64,
}

impl Default for CongestionConfig {
    fn default() -> Self {
        Self {
            min_bitrate_bps: 30_000_000,       // 30 Mbps
            max_bitrate_bps: 400_000_000,      // 400 Mbps (Wi-Fi 6/6E high bitrate)
            start_bitrate_bps: 100_000_000,    // 100 Mbps
            loss_threshold: 0.005,             // 0.5% packet loss
            max_queue_delay_ms: 6.0,           // 6ms bufferbloat queuing delay
            additive_increase_bps: 3_000_000,  // +3 Mbps per cycle
            multiplicative_decrease_factor: 0.85, // -15% on congestion
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NetworkState {
    Normal,
    Hold,
    Congested,
}

/// Dynamic Bitrate & Congestion Control Loop
/// Driven by RTT measurements and FEC loss statistics from the Quest client.
pub struct CongestionController {
    config: CongestionConfig,
    current_bitrate_bps: u64,
    min_rtt_us: u32,
    smoothed_rtt_us: f32,
    state: NetworkState,
    last_update: Instant,
    total_backoffs: u64,
}

impl CongestionController {
    pub fn new(config: CongestionConfig) -> Self {
        let current_bitrate_bps = config.start_bitrate_bps;
        Self {
            config,
            current_bitrate_bps,
            min_rtt_us: u32::MAX,
            smoothed_rtt_us: 0.0,
            state: NetworkState::Normal,
            last_update: Instant::now(),
            total_backoffs: 0,
        }
    }

    /// Processes client feedback containing RTT and packet loss statistics
    pub fn process_feedback(&mut self, feedback: &FeedbackPayload) {
        self.process_metrics(feedback.rtt_us, feedback.packets_lost, feedback.packet_loss_rate);
    }

    /// Evaluates network metrics and adjusts target bitrate
    pub fn process_metrics(&mut self, rtt_us: u32, _packets_lost: u32, loss_rate: f32) {
        if rtt_us > 0 {
            if rtt_us < self.min_rtt_us {
                self.min_rtt_us = rtt_us;
            }

            if self.smoothed_rtt_us <= 0.0 {
                self.smoothed_rtt_us = rtt_us as f32;
            } else {
                // EWMA on RTT
                self.smoothed_rtt_us = self.smoothed_rtt_us * 0.85 + (rtt_us as f32) * 0.15;
            }
        }

        // Calculate queuing delay: both instantaneous spike and smoothed delay
        let instant_queue_delay_ms = if self.min_rtt_us < u32::MAX && rtt_us >= self.min_rtt_us {
            ((rtt_us - self.min_rtt_us) as f32) / 1000.0
        } else {
            0.0
        };

        let smoothed_queue_delay_ms = if self.min_rtt_us < u32::MAX {
            ((self.smoothed_rtt_us - self.min_rtt_us as f32) / 1000.0).max(0.0)
        } else {
            0.0
        };

        let effective_queue_delay = instant_queue_delay_ms.max(smoothed_queue_delay_ms);

        // Determine network state
        if loss_rate > self.config.loss_threshold || effective_queue_delay > self.config.max_queue_delay_ms {
            // Congestion / Bufferbloat detected!
            self.state = NetworkState::Congested;
            let decreased = (self.current_bitrate_bps as f64 * self.config.multiplicative_decrease_factor) as u64;
            self.current_bitrate_bps = decreased.max(self.config.min_bitrate_bps);
            self.total_backoffs += 1;
            log::warn!(
                "[VXR-Congestion] Backoff triggered! loss={:.2}%, queue_delay={:.1}ms, new_bitrate={} Mbps",
                loss_rate * 100.0, effective_queue_delay, self.current_bitrate_bps / 1_000_000
            );
        } else if effective_queue_delay > (self.config.max_queue_delay_ms * 0.5) {
            // Approaching buffer capacity: hold bitrate steady
            self.state = NetworkState::Hold;
        } else {
            // Clean link: Additive increase
            self.state = NetworkState::Normal;
            self.current_bitrate_bps = (self.current_bitrate_bps + self.config.additive_increase_bps)
                .min(self.config.max_bitrate_bps);
        }

        self.last_update = Instant::now();
    }

    /// Calculates recommended micro-packet pacing interval in microseconds (typically 40-60 µs)
    pub fn recommended_pacing_delay_us(&self, average_packet_bytes: usize) -> u64 {
        let packet_bits = (average_packet_bytes as f64) * 8.0;
        let bitrate_bps = self.current_bitrate_bps as f64;

        let delay_sec = packet_bits / bitrate_bps;
        let delay_us = (delay_sec * 1_000_000.0) as u64;

        // Clamp to 40 - 60 microseconds window for standard streaming, or lower for ultra-high bitrates
        delay_us.clamp(20, 100)
    }

    pub fn current_bitrate_bps(&self) -> u64 {
        self.current_bitrate_bps
    }

    pub fn state(&self) -> NetworkState {
        self.state
    }

    pub fn smoothed_rtt_us(&self) -> f32 {
        self.smoothed_rtt_us
    }

    pub fn min_rtt_us(&self) -> u32 {
        self.min_rtt_us
    }

    pub fn total_backoffs(&self) -> u64 {
        self.total_backoffs
    }
}
