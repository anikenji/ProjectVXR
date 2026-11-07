use crate::resampler::FractionalResampler;

#[derive(Debug, Clone)]
pub struct DriftConfig {
    /// Desired client-side audio buffer queue in milliseconds (default: 10ms = 2 frames of 5ms)
    pub target_buffer_ms: f32,
    /// Deadband tolerance in milliseconds where no adjustment is made (default: 2ms)
    pub deadband_ms: f32,
    /// Proportional gain for PI controller
    pub kp: f64,
    /// Integral gain for PI controller
    pub ki: f64,
    /// Maximum fractional drift rate clamp (default: 0.005 = 0.5% max adjustment, inaudible)
    pub max_drift_rate: f64,
}

impl Default for DriftConfig {
    fn default() -> Self {
        Self {
            target_buffer_ms: 10.0,
            deadband_ms: 2.0,
            kp: 0.0005,
            ki: 0.00002,
            max_drift_rate: 0.005,
        }
    }
}

/// Dynamic Audio Clock Drift Compensator
/// Smoothly adjusts the PC stream sample rate to eliminate audio buffer underruns/overruns
/// on the VR headset over long streaming sessions.
pub struct AudioClockDriftCompensator {
    config: DriftConfig,
    current_ratio: f64,
    integral_error: f64,
    total_samples_in: u64,
    total_samples_out: u64,
}

impl AudioClockDriftCompensator {
    pub fn new(config: DriftConfig) -> Self {
        Self {
            config,
            current_ratio: 1.0,
            integral_error: 0.0,
            total_samples_in: 0,
            total_samples_out: 0,
        }
    }

    /// Ingests network feedback from Quest client regarding its current audio buffer queue level (in ms).
    pub fn update_client_buffer_status(&mut self, client_buffer_ms: f32) {
        let error = (client_buffer_ms - self.config.target_buffer_ms) as f64;

        if error.abs() <= self.config.deadband_ms as f64 {
            // Within deadband: smoothly decay integral error towards zero
            self.integral_error *= 0.95;
            self.current_ratio = 1.0;
            return;
        }

        // Outside deadband: run PI control loop
        self.integral_error += error;
        // Clamp integral error to avoid windup
        self.integral_error = self.integral_error.clamp(-100.0, 100.0);

        // Positive error -> Client buffer is growing (PC clock faster than Quest DAC)
        // -> We need to SLOW DOWN the effective output samples so client buffer drains.
        // Ratio < 1.0 produces fewer samples.
        let adjustment = -(self.config.kp * error + self.config.ki * self.integral_error);
        let clamped_adj = adjustment.clamp(-self.config.max_drift_rate, self.config.max_drift_rate);

        self.current_ratio = 1.0 + clamped_adj;
    }

    /// Processes an input stereo PCM float buffer, dynamically resampling if clock drift is present.
    pub fn process(&mut self, pcm_stereo: &[f32]) -> Vec<f32> {
        self.total_samples_in += pcm_stereo.len() as u64;

        if (self.current_ratio - 1.0).abs() < 1e-6 {
            self.total_samples_out += pcm_stereo.len() as u64;
            return pcm_stereo.to_vec();
        }

        let resampled = FractionalResampler::resample_stereo(pcm_stereo, self.current_ratio);
        self.total_samples_out += resampled.len() as u64;
        resampled
    }

    pub fn current_ratio(&self) -> f64 {
        self.current_ratio
    }

    pub fn drift_delta_samples(&self) -> i64 {
        self.total_samples_out as i64 - self.total_samples_in as i64
    }

    pub fn reset(&mut self) {
        self.current_ratio = 1.0;
        self.integral_error = 0.0;
    }
}
