use crate::error::Result;
use crate::mapping::{ControllerMapper, MapperConfig, VirtualGamepadReport};
use crate::predictor::{PosePredictor, PredictorConfig};
use crate::vigem::{GamepadDevice, GamepadKind, ViGEmManager};
use vxr_protocol::{HardwareCursor, InputPayload, Pose6DoF};

#[derive(Debug, Clone)]
pub struct ProcessedInput {
    pub timestamp_us: u64,
    pub predicted_hmd_pose: Pose6DoF,
    pub predicted_left_controller_pose: Pose6DoF,
    pub predicted_right_controller_pose: Pose6DoF,
    pub gamepad_report: VirtualGamepadReport,
    pub cursor: Option<HardwareCursor>,
}

pub struct VxrInputSubsystem {
    predictor: PosePredictor,
    mapper: ControllerMapper,
    gamepad: Box<dyn GamepadDevice>,
    processed_count: u64,
}

impl VxrInputSubsystem {
    pub fn new(predictor_config: PredictorConfig, mapper_config: MapperConfig, gamepad_kind: GamepadKind) -> Self {
        let vigem = ViGEmManager::new();
        let gamepad = vigem.create_gamepad(gamepad_kind);

        Self {
            predictor: PosePredictor::new(predictor_config),
            mapper: ControllerMapper::new(mapper_config),
            gamepad,
            processed_count: 0,
        }
    }

    /// Creates an input subsystem with an explicit gamepad device (e.g. MockGamepadDevice for testing)
    pub fn with_device(
        predictor_config: PredictorConfig,
        mapper_config: MapperConfig,
        gamepad: Box<dyn GamepadDevice>,
    ) -> Self {
        Self {
            predictor: PosePredictor::new(predictor_config),
            mapper: ControllerMapper::new(mapper_config),
            gamepad,
            processed_count: 0,
        }
    }

    /// Processes incoming network InputPayload:
    /// 1. Extrapolates 6DoF poses forward in time to eliminate motion-to-photon latency.
    /// 2. Maps controller inputs to virtual gamepad report (Xbox 360 / DualShock 4).
    /// 3. Updates virtual gamepad device via ViGEmBus.
    /// 4. Returns complete processed input package.
    pub fn process_input(&mut self, payload: &InputPayload, lookahead_us: Option<u64>) -> Result<ProcessedInput> {
        let ts = payload.timestamp_us;

        // 1. Extrapolate poses
        let predicted_hmd = self.predictor.update_and_predict_hmd(&payload.head_pose, ts, lookahead_us);
        let predicted_left = self.predictor.update_and_predict_controller(0, &payload.left_controller.pose, ts, lookahead_us);
        let predicted_right = self.predictor.update_and_predict_controller(1, &payload.right_controller.pose, ts, lookahead_us);

        // 2. Map controller state to virtual gamepad
        let (gamepad_report, cursor) = self.mapper.map_payload(payload);

        // 3. Update ViGEmBus virtual gamepad
        let _ = self.gamepad.update(&gamepad_report);

        self.processed_count += 1;

        Ok(ProcessedInput {
            timestamp_us: ts,
            predicted_hmd_pose: predicted_hmd,
            predicted_left_controller_pose: predicted_left,
            predicted_right_controller_pose: predicted_right,
            gamepad_report,
            cursor,
        })
    }

    pub fn processed_count(&self) -> u64 {
        self.processed_count
    }

    pub fn gamepad_device(&self) -> &dyn GamepadDevice {
        self.gamepad.as_ref()
    }

    pub fn gamepad_device_mut(&mut self) -> &mut dyn GamepadDevice {
        self.gamepad.as_mut()
    }
}
