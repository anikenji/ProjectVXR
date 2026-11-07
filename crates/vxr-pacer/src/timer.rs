use std::time::{Duration, Instant};

/// Hybrid spin/sleep timer providing sub-millisecond (microsecond) accuracy
/// on Windows by combining coarse thread sleep with tight CPU spin loop.
pub struct HybridSleeper;

impl HybridSleeper {
    /// Sleeps until `target_time` with microsecond accuracy
    #[inline]
    pub fn sleep_until(target_time: Instant) {
        let now = Instant::now();
        if now >= target_time {
            return;
        }

        let remaining = target_time - now;

        // If sleep is longer than 2ms, yield CPU for the bulk of the duration
        if remaining > Duration::from_millis(2) {
            std::thread::sleep(remaining - Duration::from_millis(1));
        }

        // Spin-wait for the final sub-millisecond precision
        while Instant::now() < target_time {
            std::hint::spin_loop();
        }
    }

    /// Sleeps for the specified number of microseconds
    #[inline]
    pub fn sleep_micros(micros: u64) {
        let target = Instant::now() + Duration::from_micros(micros);
        Self::sleep_until(target);
    }
}
