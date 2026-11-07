use vxr_protocol::Pose6DoF;

#[derive(Debug, Clone)]
pub struct PredictorConfig {
    /// Default lookahead in microseconds (e.g. 20,000 us = 20ms)
    pub default_lookahead_us: u64,
    /// Maximum lookahead clamp in microseconds (e.g. 50,000 us = 50ms)
    pub max_lookahead_us: u64,
    /// Acceleration smoothing factor (0.0 = only current, 0.8 = strong smoothing)
    pub accel_smoothing: f32,
    /// Maximum linear acceleration clamp in m/s^2 to filter tracking outliers
    pub max_accel: f32,
}

impl Default for PredictorConfig {
    fn default() -> Self {
        Self {
            default_lookahead_us: 20_000, // 20ms
            max_lookahead_us: 50_000,     // 50ms
            accel_smoothing: 0.5,
            max_accel: 50.0,              // 50 m/s^2 (~5G)
        }
    }
}

/// Historical pose sample for numerical differentiation & smoothing
#[derive(Debug, Clone)]
struct PoseSample {
    timestamp_us: u64,
    pose: Pose6DoF,
}

/// 6DoF Pose Predictor extrapolating head and controller poses forward in time
/// to eliminate perceptual latency across network transport and display presentation.
pub struct PosePredictor {
    config: PredictorConfig,
    hmd_history: Vec<PoseSample>,
    left_history: Vec<PoseSample>,
    right_history: Vec<PoseSample>,
    smoothed_hmd_accel: [f32; 3],
}

impl PosePredictor {
    pub fn new(config: PredictorConfig) -> Self {
        Self {
            config,
            hmd_history: Vec::with_capacity(8),
            left_history: Vec::with_capacity(8),
            right_history: Vec::with_capacity(8),
            smoothed_hmd_accel: [0.0; 3],
        }
    }

    /// Extrapolates a 6DoF pose forward by `lookahead_us`
    pub fn extrapolate_pose(pose: &Pose6DoF, accel: [f32; 3], lookahead_us: u64) -> Pose6DoF {
        let dt = (lookahead_us as f32) / 1_000_000.0; // convert to seconds
        if dt <= 0.0 {
            return *pose;
        }

        // 1. Position extrapolation: p(t + dt) = p + v * dt + 0.5 * a * dt^2
        let mut predicted_pos = [0.0f32; 3];
        for i in 0..3 {
            predicted_pos[i] = pose.position[i] + pose.linear_velocity[i] * dt + 0.5 * accel[i] * dt * dt;
        }

        // 2. Orientation extrapolation via angular velocity integration
        let wx = pose.angular_velocity[0];
        let wy = pose.angular_velocity[1];
        let wz = pose.angular_velocity[2];
        let omega_mag = (wx * wx + wy * wy + wz * wz).sqrt();

        let predicted_rot = if omega_mag > 1e-5 {
            let theta = omega_mag * dt;
            let half_theta = theta * 0.5;
            let sin_half = half_theta.sin();
            let cos_half = half_theta.cos();

            let norm_x = wx / omega_mag;
            let norm_y = wy / omega_mag;
            let norm_z = wz / omega_mag;

            let delta_q = [
                norm_x * sin_half,
                norm_y * sin_half,
                norm_z * sin_half,
                cos_half,
            ];

            // Quaternion multiplication: delta_q * pose.orientation
            Self::quat_mul(&delta_q, &pose.orientation)
        } else {
            pose.orientation
        };

        // 3. Velocities: v(t + dt) = v + a * dt
        let mut predicted_vel = [0.0f32; 3];
        for i in 0..3 {
            predicted_vel[i] = pose.linear_velocity[i] + accel[i] * dt;
        }

        Pose6DoF {
            position: predicted_pos,
            orientation: predicted_rot,
            linear_velocity: predicted_vel,
            angular_velocity: pose.angular_velocity,
        }
    }

    /// Updates HMD pose sample and returns predicted pose
    pub fn update_and_predict_hmd(&mut self, pose: &Pose6DoF, timestamp_us: u64, lookahead_us: Option<u64>) -> Pose6DoF {
        let dt_lookahead = lookahead_us.unwrap_or(self.config.default_lookahead_us).min(self.config.max_lookahead_us);

        // Compute numerical acceleration if history available
        let mut current_accel = [0.0f32; 3];
        if let Some(last) = self.hmd_history.last() {
            let dt = (timestamp_us.saturating_sub(last.timestamp_us) as f32) / 1_000_000.0;
            if dt > 1e-4 && dt < 0.1 {
                for (i, accel) in current_accel.iter_mut().enumerate() {
                    let dv = pose.linear_velocity[i] - last.pose.linear_velocity[i];
                    let raw_accel = dv / dt;
                    *accel = raw_accel.clamp(-self.config.max_accel, self.config.max_accel);
                }
            }
        }

        // Exponential smoothing on acceleration
        for (smoothed, &current) in self.smoothed_hmd_accel.iter_mut().zip(&current_accel) {
            *smoothed = *smoothed * self.config.accel_smoothing
                + current * (1.0 - self.config.accel_smoothing);
        }

        // Store in history buffer (retain last 8)
        self.hmd_history.push(PoseSample { timestamp_us, pose: *pose });
        if self.hmd_history.len() > 8 {
            self.hmd_history.remove(0);
        }

        Self::extrapolate_pose(pose, self.smoothed_hmd_accel, dt_lookahead)
    }

    /// Updates Controller pose sample and returns predicted pose
    pub fn update_and_predict_controller(&mut self, hand: usize, pose: &Pose6DoF, timestamp_us: u64, lookahead_us: Option<u64>) -> Pose6DoF {
        let dt_lookahead = lookahead_us.unwrap_or(self.config.default_lookahead_us).min(self.config.max_lookahead_us);
        let history = if hand == 0 { &mut self.left_history } else { &mut self.right_history };

        history.push(PoseSample { timestamp_us, pose: *pose });
        if history.len() > 8 {
            history.remove(0);
        }

        // Controllers typically extrapolate with velocity + zero-acceleration for stability
        Self::extrapolate_pose(pose, [0.0; 3], dt_lookahead)
    }

    /// Hamilton quaternion product: q1 * q2
    /// Quaternion format: [x, y, z, w]
    pub fn quat_mul(q1: &[f32; 4], q2: &[f32; 4]) -> [f32; 4] {
        let x1 = q1[0]; let y1 = q1[1]; let z1 = q1[2]; let w1 = q1[3];
        let x2 = q2[0]; let y2 = q2[1]; let z2 = q2[2]; let w2 = q2[3];

        let w = w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2;
        let x = w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2;
        let y = w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2;
        let z = w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2;

        let norm = (x * x + y * y + z * z + w * w).sqrt();
        if norm > 1e-6 {
            [x / norm, y / norm, z / norm, w / norm]
        } else {
            [0.0, 0.0, 0.0, 1.0]
        }
    }
}
