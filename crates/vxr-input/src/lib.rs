//! # VXR Input
//!
//! Subsystem for 6DoF pose prediction, VR controller input mapping,
//! and virtual gamepad emulation (Xbox 360 / DualShock 4) via ViGEmBus.

pub mod error;
pub mod mapping;
pub mod predictor;
pub mod subsystem;
pub mod vigem;

pub use error::{InputError, Result};
pub use mapping::{buttons, ControllerMapper, MapperConfig, VirtualGamepadReport};
pub use predictor::{PosePredictor, PredictorConfig};
pub use subsystem::{ProcessedInput, VxrInputSubsystem};
pub use vigem::{GamepadDevice, GamepadKind, MockGamepadDevice, ViGEmManager, ViGEmXbox360};

#[cfg(test)]
mod tests {
    use super::*;
    use vxr_protocol::{ControllerState, HardwareCursor, InputPayload, Pose6DoF};

    #[test]
    fn test_pose_predictor_linear_and_angular() {
        let config = PredictorConfig {
            default_lookahead_us: 20_000, // 20ms
            max_lookahead_us: 50_000,
            accel_smoothing: 0.5,
            max_accel: 50.0,
        };
        let mut predictor = PosePredictor::new(config);

        // Initial pose at t=0 moving forward at 1 m/s along Z and rotating around Y at 1 rad/s
        let p1 = Pose6DoF {
            position: [0.0, 1.7, 0.0],
            orientation: [0.0, 0.0, 0.0, 1.0], // Identity rotation
            linear_velocity: [0.0, 0.0, -1.0], // -1 m/s along Z
            angular_velocity: [0.0, 1.0, 0.0], // 1 rad/s around Y
        };

        // Predict with 20ms lookahead
        let pred = predictor.update_and_predict_hmd(&p1, 1_000_000, Some(20_000));

        // In 20ms at -1 m/s, delta Z = -0.02m
        assert!((pred.position[2] - (-0.02)).abs() < 1e-4);
        assert!((pred.position[1] - 1.7).abs() < 1e-4);

        // In 20ms at 1 rad/s around Y, quaternion must rotate around Y
        assert!(pred.orientation[1] > 0.005, "Orientation must rotate around Y axis");
        assert!(pred.orientation[3] < 1.0, "W component must reflect rotation");

        // Verify quaternion normalization: norm == 1.0
        let norm = (pred.orientation[0].powi(2)
            + pred.orientation[1].powi(2)
            + pred.orientation[2].powi(2)
            + pred.orientation[3].powi(2))
        .sqrt();
        assert!((norm - 1.0).abs() < 1e-4, "Predicted quaternion must be normalized");
    }

    #[test]
    fn test_controller_mapping_deadzone_and_buttons() {
        let mapper = ControllerMapper::new(MapperConfig::default());

        let mut left = ControllerState::default();
        let mut right = ControllerState::default();

        // 1. Within deadzone: 5% stick should be clamped to 0
        left.thumbstick = [0.05, 0.03];
        let rep1 = mapper.map_controllers(&left, &right);
        assert_eq!(rep1.thumb_lx, 0);
        assert_eq!(rep1.thumb_ly, 0);

        // 2. Outside deadzone: 80% stick should map to ~27000
        left.thumbstick = [0.8, -0.8];
        let rep2 = mapper.map_controllers(&left, &right);
        assert!(rep2.thumb_lx > 20000);
        assert!(rep2.thumb_ly < -20000);

        // 3. Triggers & Grips
        left.trigger_value = 0.95;
        right.trigger_value = 0.50;
        left.grip_value = 0.85; // triggers Left Shoulder (LB)
        right.buttons = 0x01 | 0x02; // A and B buttons
        left.buttons = 0x01 | 0x04;  // X button and Thumbstick click (LS)

        let rep3 = mapper.map_controllers(&left, &right);
        assert!(rep3.left_trigger > 230);
        assert!(rep3.right_trigger > 120 && rep3.right_trigger < 140);
        assert_ne!(rep3.buttons & buttons::LEFT_SHOULDER, 0, "Grip >= 60% must trigger Left Shoulder");
        assert_ne!(rep3.buttons & buttons::A, 0, "Button A must be pressed");
        assert_ne!(rep3.buttons & buttons::B, 0, "Button B must be pressed");
        assert_ne!(rep3.buttons & buttons::X, 0, "Button X must be pressed");
        assert_ne!(rep3.buttons & buttons::LEFT_THUMB, 0, "Left stick click must be pressed");
    }

    #[test]
    fn test_mock_gamepad_device() {
        let mut mock = MockGamepadDevice::new("Test Virtual Gamepad");
        assert!(!mock.is_real_hardware());
        assert_eq!(mock.update_count(), 0);

        let report = VirtualGamepadReport {
            buttons: buttons::A | buttons::START,
            left_trigger: 200,
            right_trigger: 100,
            thumb_lx: 15000,
            thumb_ly: -15000,
            thumb_rx: 0,
            thumb_ry: 0,
        };

        mock.update(&report).expect("Update mock gamepad");
        assert_eq!(mock.update_count(), 1);
        assert_eq!(mock.last_report(), report);
    }

    #[test]
    fn test_input_subsystem_end_to_end() {
        let mock_device = Box::new(MockGamepadDevice::new("Subsystem Mock"));
        let mut subsystem = VxrInputSubsystem::with_device(
            PredictorConfig::default(),
            MapperConfig::default(),
            mock_device,
        );

        let payload = InputPayload {
            timestamp_us: 1_234_567,
            head_pose: Pose6DoF {
                position: [0.1, 1.8, -0.3],
                orientation: [0.0, 0.0, 0.0, 1.0],
                linear_velocity: [0.0, 0.0, -0.5],
                angular_velocity: [0.0, 0.0, 0.0],
            },
            left_controller: ControllerState {
                is_active: true,
                thumbstick: [0.75, 0.0],
                trigger_value: 0.9,
                ..Default::default()
            },
            right_controller: ControllerState {
                is_active: true,
                buttons: 0x01, // Button A
                ..Default::default()
            },
            cursor: Some(HardwareCursor {
                x: 0.65,
                y: 0.45,
                visible: true,
            }),
        };

        let result = subsystem.process_input(&payload, Some(20_000)).expect("Process input");

        assert_eq!(result.timestamp_us, 1_234_567);
        // HMD position should be extrapolated forward
        assert!(result.predicted_hmd_pose.position[2] < -0.3);
        // Gamepad inputs mapped
        assert!(result.gamepad_report.thumb_lx > 15000);
        assert!(result.gamepad_report.left_trigger > 200);
        assert_ne!(result.gamepad_report.buttons & buttons::A, 0);
        // Hardware cursor preserved
        assert_eq!(result.cursor, Some(HardwareCursor { x: 0.65, y: 0.45, visible: true }));
        // Subsystem stats
        assert_eq!(subsystem.processed_count(), 1);
    }
}
