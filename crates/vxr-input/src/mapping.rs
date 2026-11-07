use vxr_protocol::{ControllerState, HardwareCursor, InputPayload};

pub mod buttons {
    pub const DPAD_UP: u16        = 0x0001;
    pub const DPAD_DOWN: u16      = 0x0002;
    pub const DPAD_LEFT: u16      = 0x0004;
    pub const DPAD_RIGHT: u16     = 0x0008;
    pub const START: u16          = 0x0010;
    pub const BACK: u16           = 0x0020;
    pub const LEFT_THUMB: u16     = 0x0040; // LS / L3
    pub const RIGHT_THUMB: u16    = 0x0080; // RS / R3
    pub const LEFT_SHOULDER: u16  = 0x0100; // LB / L1
    pub const RIGHT_SHOULDER: u16 = 0x0200; // RB / R1
    pub const GUIDE: u16          = 0x0400; // Xbox Guide / PS Button
    pub const A: u16              = 0x1000; // Cross on DS4
    pub const B: u16              = 0x2000; // Circle on DS4
    pub const X: u16              = 0x4000; // Square on DS4
    pub const Y: u16              = 0x8000; // Triangle on DS4
}

/// Generic Virtual Gamepad Report (compatible with Xbox 360 and DualShock 4)
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct VirtualGamepadReport {
    pub buttons: u16,
    pub left_trigger: u8,
    pub right_trigger: u8,
    pub thumb_lx: i16,
    pub thumb_ly: i16,
    pub thumb_rx: i16,
    pub thumb_ry: i16,
}

#[derive(Debug, Clone)]
pub struct MapperConfig {
    pub stick_deadzone: f32, // e.g. 0.08 = 8% radial deadband
    pub trigger_deadzone: f32,
    pub grip_as_shoulder_threshold: f32, // e.g. 0.6 = 60% grip presses LB/RB
    pub invert_y: bool,
}

impl Default for MapperConfig {
    fn default() -> Self {
        Self {
            stick_deadzone: 0.08,
            trigger_deadzone: 0.05,
            grip_as_shoulder_threshold: 0.6,
            invert_y: false,
        }
    }
}

pub struct ControllerMapper {
    config: MapperConfig,
}

impl ControllerMapper {
    pub fn new(config: MapperConfig) -> Self {
        Self { config }
    }

    /// Maps VR Controllers (Left & Right) to a virtual Xbox / DualShock gamepad report
    pub fn map_controllers(&self, left: &ControllerState, right: &ControllerState) -> VirtualGamepadReport {
        let mut report = VirtualGamepadReport::default();
        let mut btn_mask: u16 = 0;

        // 1. Thumbsticks
        let (lx, ly) = self.apply_deadzone(left.thumbstick[0], left.thumbstick[1]);
        let (rx, ry) = self.apply_deadzone(right.thumbstick[0], right.thumbstick[1]);

        report.thumb_lx = (lx * 32767.0).clamp(-32768.0, 32767.0) as i16;
        report.thumb_ly = ((if self.config.invert_y { -ly } else { ly }) * 32767.0).clamp(-32768.0, 32767.0) as i16;

        report.thumb_rx = (rx * 32767.0).clamp(-32768.0, 32767.0) as i16;
        report.thumb_ry = ((if self.config.invert_y { -ry } else { ry }) * 32767.0).clamp(-32768.0, 32767.0) as i16;

        // 2. Triggers
        let lt = if left.trigger_value > self.config.trigger_deadzone {
            (left.trigger_value * 255.0).clamp(0.0, 255.0) as u8
        } else {
            0
        };
        let rt = if right.trigger_value > self.config.trigger_deadzone {
            (right.trigger_value * 255.0).clamp(0.0, 255.0) as u8
        } else {
            0
        };

        report.left_trigger = lt;
        report.right_trigger = rt;

        // 3. Grips as Bumpers (LB / RB)
        if left.grip_value >= self.config.grip_as_shoulder_threshold {
            btn_mask |= buttons::LEFT_SHOULDER;
        }
        if right.grip_value >= self.config.grip_as_shoulder_threshold {
            btn_mask |= buttons::RIGHT_SHOULDER;
        }

        // 4. Buttons: Right controller (A / B)
        // Bit 0 = A, Bit 1 = B, Bit 2 = Thumbstick click
        if (right.buttons & 0x01) != 0 {
            btn_mask |= buttons::A;
        }
        if (right.buttons & 0x02) != 0 {
            btn_mask |= buttons::B;
        }
        if (right.buttons & 0x04) != 0 {
            btn_mask |= buttons::RIGHT_THUMB;
        }

        // 5. Buttons: Left controller (X / Y)
        // Bit 0 = X, Bit 1 = Y, Bit 2 = Thumbstick click, Bit 3 = Menu
        if (left.buttons & 0x01) != 0 {
            btn_mask |= buttons::X;
        }
        if (left.buttons & 0x02) != 0 {
            btn_mask |= buttons::Y;
        }
        if (left.buttons & 0x04) != 0 {
            btn_mask |= buttons::LEFT_THUMB;
        }
        if (left.buttons & 0x08) != 0 {
            btn_mask |= buttons::BACK;
        }

        report.buttons = btn_mask;
        report
    }

    /// Maps entire InputPayload to gamepad report and mouse cursor
    pub fn map_payload(&self, payload: &InputPayload) -> (VirtualGamepadReport, Option<HardwareCursor>) {
        let gamepad = self.map_controllers(&payload.left_controller, &payload.right_controller);
        (gamepad, payload.cursor)
    }

    fn apply_deadzone(&self, x: f32, y: f32) -> (f32, f32) {
        let mag = (x * x + y * y).sqrt();
        if mag < self.config.stick_deadzone {
            return (0.0, 0.0);
        }

        let normalized_mag = (mag - self.config.stick_deadzone) / (1.0 - self.config.stick_deadzone);
        let scale = normalized_mag / mag;
        ((x * scale).clamp(-1.0, 1.0), (y * scale).clamp(-1.0, 1.0))
    }
}
