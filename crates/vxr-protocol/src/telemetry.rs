//! Phase D: Binary Telemetry & Frame Metadata Wire Structs (`VxrFrameData` & `VxrFrameMetadata`)
//!
//! Zero-overhead packed binary telemetry structures (Headset -> PC at 72/90/120Hz)
//! and frame synchronization metadata (PC -> Headset for Asynchronous Timewarp).

use crate::error::{ProtocolError, Result};

/// Fixed 4,992-byte binary telemetry structure matching Phase D.1 specification.
pub const VXR_FRAME_DATA_WIRE_SIZE: usize = 4992;

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VxrJointPose {
    pub position: [f32; 3],
    pub orientation: [f32; 4],
    pub radius: f32,
}

impl Default for VxrJointPose {
    fn default() -> Self {
        Self {
            position: [0.0; 3],
            orientation: [0.0, 0.0, 0.0, 1.0],
            radius: 0.01,
        }
    }
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VxrHandTracking26 {
    pub is_tracked: u8,
    pub confidence: u8,
    pub pinch_mask: u8,
    pub reserved: u8,
    pub joints: [VxrJointPose; 26], // 26 * 32 = 832 bytes
}

impl Default for VxrHandTracking26 {
    fn default() -> Self {
        Self {
            is_tracked: 0,
            confidence: 0,
            pinch_mask: 0,
            reserved: 0,
            joints: [VxrJointPose::default(); 26],
        }
    }
}

#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VxrTouchControllerTelemetry {
    pub is_tracked: u8,
    pub buttons_mask: u16,
    pub touches_mask: u16,
    pub battery_pct: u8,
    pub reserved: [u8; 2],
    pub position: [f32; 3],
    pub orientation: [f32; 4],
    pub linear_velocity: [f32; 3],
    pub angular_velocity: [f32; 3],
    pub trigger_value: f32,
    pub grip_value: f32,
    pub thumbstick: [f32; 2],
}

impl Default for VxrTouchControllerTelemetry {
    fn default() -> Self {
        Self {
            is_tracked: 0,
            buttons_mask: 0,
            touches_mask: 0,
            battery_pct: 100,
            reserved: [0; 2],
            position: [0.0; 3],
            orientation: [0.0, 0.0, 0.0, 1.0],
            linear_velocity: [0.0; 3],
            angular_velocity: [0.0; 3],
            trigger_value: 0.0,
            grip_value: 0.0,
            thumbstick: [0.0; 2],
        }
    }
}

/// Phase D.1: `VxrFrameData` (Headset -> PC over UDP at V-Sync rate)
/// Includes sequence number, measured RTT, guardian floor height, mouse/keyboard state,
/// 6DoF Headset pose, 2x Quest Touch controllers, 2x 26-joint Hand Tracking, and Eye Gaze Ray.
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VxrFrameData {
    pub magic: u32,               // 0x56585254 ("VXRT")
    pub sequence_number: u64,
    pub client_timestamp_us: u64,
    pub measured_rtt_ms: f32,
    pub floor_height_m: f32,
    pub mouse_norm_x: f32,
    pub mouse_norm_y: f32,
    pub mouse_buttons: u8,
    pub key_modifiers: u8,
    pub virtual_keycode: u16,
    pub head_position: [f32; 3],
    pub head_orientation: [f32; 4],
    pub head_linear_velocity: [f32; 3],
    pub head_angular_velocity: [f32; 3],
    pub eye_gaze_origin: [f32; 3],
    pub eye_gaze_direction: [f32; 3],
    pub controllers: [VxrTouchControllerTelemetry; 2],
    pub hands: [VxrHandTracking26; 2],
    pub reserved_extension: [u8; 3052],
}

impl Default for VxrFrameData {
    fn default() -> Self {
        Self {
            magic: 0x5658_5254,
            sequence_number: 0,
            client_timestamp_us: 0,
            measured_rtt_ms: 0.0,
            floor_height_m: 1.5,
            mouse_norm_x: 0.5,
            mouse_norm_y: 0.5,
            mouse_buttons: 0,
            key_modifiers: 0,
            virtual_keycode: 0,
            head_position: [0.0, 1.65, 0.0],
            head_orientation: [0.0, 0.0, 0.0, 1.0],
            head_linear_velocity: [0.0; 3],
            head_angular_velocity: [0.0; 3],
            eye_gaze_origin: [0.0, 1.65, 0.0],
            eye_gaze_direction: [0.0, 0.0, -1.0],
            controllers: [VxrTouchControllerTelemetry::default(); 2],
            hands: [VxrHandTracking26::default(); 2],
            reserved_extension: [0u8; 3052],
        }
    }
}

impl VxrFrameData {
    pub fn to_bytes(&self) -> [u8; VXR_FRAME_DATA_WIRE_SIZE] {
        debug_assert_eq!(std::mem::size_of::<Self>(), VXR_FRAME_DATA_WIRE_SIZE);
        unsafe { std::mem::transmute_copy(self) }
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<Self> {
        if bytes.len() < VXR_FRAME_DATA_WIRE_SIZE {
            return Err(ProtocolError::BufferTooSmall {
                expected: VXR_FRAME_DATA_WIRE_SIZE,
                actual: bytes.len(),
            });
        }
        let mut raw = [0u8; VXR_FRAME_DATA_WIRE_SIZE];
        raw.copy_from_slice(&bytes[..VXR_FRAME_DATA_WIRE_SIZE]);
        let pkt: Self = unsafe { std::mem::transmute(raw) };
        if pkt.magic != 0x5658_5254 {
            return Err(ProtocolError::InvalidMagic(pkt.magic));
        }
        Ok(pkt)
    }
}

/// Phase D.2: `VxrFrameMetadata` (PC -> Headset header for Asynchronous Timewarp)
#[repr(C, packed)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VxrFrameMetadata {
    pub magic: u32,               // 0x5658524D ("VXRM")
    pub frame_index: u64,
    pub timestamp_us: u64,
    pub render_pose_position: [f32; 3],
    pub render_pose_orientation: [f32; 4],
    pub haptic_freq_hz: [f32; 2],
    pub haptic_amplitude: [f32; 2],
}

impl Default for VxrFrameMetadata {
    fn default() -> Self {
        Self {
            magic: 0x5658_524D,
            frame_index: 0,
            timestamp_us: 0,
            render_pose_position: [0.0, 1.65, 0.0],
            render_pose_orientation: [0.0, 0.0, 0.0, 1.0],
            haptic_freq_hz: [0.0; 2],
            haptic_amplitude: [0.0; 2],
        }
    }
}
