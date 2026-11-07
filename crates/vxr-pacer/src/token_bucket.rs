use std::time::{Duration, Instant};

/// Token Bucket rate limiter to prevent Wi-Fi router bufferbloat and packet drops.
pub struct TokenBucket {
    rate_bytes_per_sec: f64,
    capacity_bytes: f64,
    tokens: f64,
    last_update: Instant,
}

impl TokenBucket {
    pub fn new(bitrate_bps: u64, burst_capacity_bytes: usize) -> Self {
        let rate_bytes_per_sec = (bitrate_bps as f64) / 8.0;
        let capacity_bytes = burst_capacity_bytes as f64;

        Self {
            rate_bytes_per_sec,
            capacity_bytes,
            tokens: capacity_bytes, // start full
            last_update: Instant::now(),
        }
    }

    /// Replenishes tokens based on elapsed wall-clock time
    pub fn replenish(&mut self) {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last_update).as_secs_f64();
        self.last_update = now;

        self.tokens = (self.tokens + elapsed * self.rate_bytes_per_sec).min(self.capacity_bytes);
    }

    /// Attempts to consume `bytes` from the token bucket.
    /// If sufficient tokens exist, returns `Ok(())`.
    /// Otherwise, returns `Err(Duration)` indicating the required wait duration before tokens refill.
    pub fn try_consume(&mut self, bytes: usize) -> Result<(), Duration> {
        self.replenish();

        let needed = bytes as f64;
        if self.tokens >= needed {
            self.tokens -= needed;
            Ok(())
        } else {
            let deficit = needed - self.tokens;
            let wait_secs = deficit / self.rate_bytes_per_sec;
            Err(Duration::from_secs_f64(wait_secs))
        }
    }

    /// Dynamically updates the rate limit (in bits per second) from the congestion control loop
    pub fn set_bitrate(&mut self, bitrate_bps: u64) {
        self.replenish();
        self.rate_bytes_per_sec = ((bitrate_bps as f64) / 8.0).max(100_000.0); // at least 100 KB/s
    }

    pub fn current_bitrate_bps(&self) -> u64 {
        (self.rate_bytes_per_sec * 8.0) as u64
    }

    pub fn available_tokens(&mut self) -> usize {
        self.replenish();
        self.tokens.max(0.0) as usize
    }
}
