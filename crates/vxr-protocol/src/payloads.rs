use byteorder::{ByteOrder, LittleEndian};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use crate::error::{ProtocolError, Result};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum VideoCodec {
    H264 = 1,
    Hevc = 2,
    Av1 = 3,
}

impl VideoCodec {
    pub fn from_u8(v: u8) -> Result<Self> {
        match v {
            1 => Ok(VideoCodec::H264),
            2 => Ok(VideoCodec::Hevc),
            3 => Ok(VideoCodec::Av1),
            _ => Err(ProtocolError::Deserialization(format!("Unknown video codec id: {}", v))),
        }
    }
}

/// Video frame slice payload header: 24 bytes
/// [0..4]: frame_index (u32)
/// [4..6]: fragment_index (u16)
/// [6..8]: total_fragments (u16)
/// [8]: codec (u8)
/// [9]: flags (u8: bit 0 = keyframe, bit 1 = intra_refresh, bit 2 = sliced)
/// [10..16]: reserved (6 bytes)
/// [16..24]: pts_us (u64)
/// Followed by raw compressed NAL / bitstream data.
pub const VIDEO_PAYLOAD_HEADER_SIZE: usize = 24;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct VideoPayload {
    pub frame_index: u32,
    pub fragment_index: u16,
    pub total_fragments: u16,
    pub codec: VideoCodec,
    pub is_keyframe: bool,
    pub is_intra_refresh: bool,
    pub is_sliced: bool,
    pub pts_us: u64,
    pub data: Vec<u8>,
}

impl VideoPayload {
    pub fn encode(&self) -> Vec<u8> {
        let mut buf = vec![0u8; VIDEO_PAYLOAD_HEADER_SIZE + self.data.len()];
        LittleEndian::write_u32(&mut buf[0..4], self.frame_index);
        LittleEndian::write_u16(&mut buf[4..6], self.fragment_index);
        LittleEndian::write_u16(&mut buf[6..8], self.total_fragments);
        buf[8] = self.codec as u8;

        let mut flags = 0u8;
        if self.is_keyframe { flags |= 0x01; }
        if self.is_intra_refresh { flags |= 0x02; }
        if self.is_sliced { flags |= 0x04; }
        buf[9] = flags;

        // bytes 10..16 reserved
        LittleEndian::write_u64(&mut buf[16..24], self.pts_us);
        buf[VIDEO_PAYLOAD_HEADER_SIZE..].copy_from_slice(&self.data);
        buf
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        if src.len() < VIDEO_PAYLOAD_HEADER_SIZE {
            return Err(ProtocolError::BufferTooShort {
                expected: VIDEO_PAYLOAD_HEADER_SIZE,
                actual: src.len(),
            });
        }

        let frame_index = LittleEndian::read_u32(&src[0..4]);
        let fragment_index = LittleEndian::read_u16(&src[4..6]);
        let total_fragments = LittleEndian::read_u16(&src[6..8]);
        let codec = VideoCodec::from_u8(src[8])?;

        let flags = src[9];
        let is_keyframe = (flags & 0x01) != 0;
        let is_intra_refresh = (flags & 0x02) != 0;
        let is_sliced = (flags & 0x04) != 0;

        let pts_us = LittleEndian::read_u64(&src[16..24]);
        let data = src[VIDEO_PAYLOAD_HEADER_SIZE..].to_vec();

        Ok(Self {
            frame_index,
            fragment_index,
            total_fragments,
            codec,
            is_keyframe,
            is_intra_refresh,
            is_sliced,
            pts_us,
            data,
        })
    }
}

/// Splits a full video frame into MTU-sized video payloads
pub struct VideoFragmenter;

impl VideoFragmenter {
    #[allow(clippy::too_many_arguments)]
    pub fn fragment_frame(
        frame_index: u32,
        codec: VideoCodec,
        is_keyframe: bool,
        is_intra_refresh: bool,
        is_sliced: bool,
        pts_us: u64,
        frame_data: &[u8],
        max_chunk_size: usize,
    ) -> Vec<VideoPayload> {
        if frame_data.is_empty() {
            return vec![VideoPayload {
                frame_index,
                fragment_index: 0,
                total_fragments: 1,
                codec,
                is_keyframe,
                is_intra_refresh,
                is_sliced,
                pts_us,
                data: Vec::new(),
            }];
        }

        let chunks: Vec<&[u8]> = frame_data.chunks(max_chunk_size).collect();
        let total_fragments = chunks.len() as u16;

        chunks
            .into_iter()
            .enumerate()
            .map(|(i, chunk)| VideoPayload {
                frame_index,
                fragment_index: i as u16,
                total_fragments,
                codec,
                is_keyframe,
                is_intra_refresh,
                is_sliced,
                pts_us,
                data: chunk.to_vec(),
            })
            .collect()
    }
}

/// Frame buffer collector to reassemble fragmented video frames
pub struct VideoReassembler {
    frames: HashMap<u32, FrameAssemblyState>,
    max_retained_frames: usize,
}

struct FrameAssemblyState {
    codec: VideoCodec,
    is_keyframe: bool,
    pts_us: u64,
    total_fragments: u16,
    received_count: u16,
    fragments: Vec<Option<Vec<u8>>>,
}

impl VideoReassembler {
    pub fn new(max_retained_frames: usize) -> Self {
        Self {
            frames: HashMap::new(),
            max_retained_frames,
        }
    }

    pub fn insert(&mut self, payload: VideoPayload) -> Option<(u32, VideoCodec, bool, u64, Vec<u8>)> {
        let frame_index = payload.frame_index;

        let state = self.frames.entry(frame_index).or_insert_with(|| {
            let total = payload.total_fragments as usize;
            FrameAssemblyState {
                codec: payload.codec,
                is_keyframe: payload.is_keyframe,
                pts_us: payload.pts_us,
                total_fragments: payload.total_fragments,
                received_count: 0,
                fragments: vec![None; total],
            }
        });

        let idx = payload.fragment_index as usize;
        if idx < state.fragments.len() && state.fragments[idx].is_none() {
            state.fragments[idx] = Some(payload.data);
            state.received_count += 1;
        }

        if state.received_count == state.total_fragments {
            if let Some(completed) = self.frames.remove(&frame_index) {
                let mut assembled = Vec::new();
                for bytes in completed.fragments.into_iter().flatten() {
                    assembled.extend_from_slice(&bytes);
                }

                // Cleanup older frames if capacity reached
                if self.frames.len() > self.max_retained_frames {
                    let min_key = self.frames.keys().copied().min();
                    if let Some(k) = min_key {
                        self.frames.remove(&k);
                    }
                }

                return Some((
                    frame_index,
                    completed.codec,
                    completed.is_keyframe,
                    completed.pts_us,
                    assembled,
                ));
            }
        }

        None
    }
}

/// Audio payload (Opus packets)
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AudioPayload {
    pub sequence_number: u32,
    pub timestamp_us: u64,
    pub channels: u8,
    pub sample_rate: u32,
    pub data: Vec<u8>,
}

impl AudioPayload {
    pub fn encode(&self) -> Result<Vec<u8>> {
        bincode::serialize(self).map_err(|e| ProtocolError::Serialization(e.to_string()))
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        bincode::deserialize(src).map_err(|e| ProtocolError::Deserialization(e.to_string()))
    }
}

/// 6DoF Pose in 3D Space
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
pub struct Pose6DoF {
    pub position: [f32; 3],       // x, y, z (meters)
    pub orientation: [f32; 4],    // x, y, z, w (quaternion)
    pub linear_velocity: [f32; 3], // m/s
    pub angular_velocity: [f32; 3], // rad/s
}

/// Controller state
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct ControllerState {
    pub is_active: bool,
    pub pose: Pose6DoF,
    pub buttons: u32,
    pub trigger_value: f32,
    pub grip_value: f32,
    pub thumbstick: [f32; 2],
    pub battery_level: u8,
}

/// Hardware cursor position for local 0ms rendering
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize, Default)]
pub struct HardwareCursor {
    pub x: f32,
    pub y: f32,
    pub visible: bool,
}

/// Input payload
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct InputPayload {
    pub timestamp_us: u64,
    pub head_pose: Pose6DoF,
    pub left_controller: ControllerState,
    pub right_controller: ControllerState,
    pub cursor: Option<HardwareCursor>,
}

impl InputPayload {
    pub fn encode(&self) -> Result<Vec<u8>> {
        bincode::serialize(self).map_err(|e| ProtocolError::Serialization(e.to_string()))
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        bincode::deserialize(src).map_err(|e| ProtocolError::Deserialization(e.to_string()))
    }
}

/// Control messages (Handshake, Ping/Pong, KeepAlive, Disconnect)
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum ControlPayload {
    HandshakeRequest {
        client_version: u32,
        headset_model: String,
        preferred_codec: VideoCodec,
        display_width: u32,
        display_height: u32,
        refresh_rate: u32,
    },
    HandshakeResponse {
        server_version: u32,
        session_id: u64,
        accepted: bool,
        codec: VideoCodec,
        stream_width: u32,
        stream_height: u32,
        fps: u32,
    },
    Ping {
        sequence: u32,
        send_time_us: u64,
    },
    Pong {
        sequence: u32,
        ping_send_time_us: u64,
        server_recv_time_us: u64,
        server_send_time_us: u64,
    },
    KeepAlive {
        timestamp_us: u64,
    },
    Disconnect {
        reason_code: u32,
        message: String,
    },
}

impl ControlPayload {
    pub fn encode(&self) -> Result<Vec<u8>> {
        bincode::serialize(self).map_err(|e| ProtocolError::Serialization(e.to_string()))
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        bincode::deserialize(src).map_err(|e| ProtocolError::Deserialization(e.to_string()))
    }
}

/// Feedback from client to host for bitrate & congestion control
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct FeedbackPayload {
    pub last_frame_received: u32,
    pub frames_lost: u32,
    pub packets_lost: u32,
    pub packet_loss_rate: f32,
    pub rtt_us: u32,
    pub suggested_bitrate_bps: u32,
    pub battery_level: u8,
}

impl FeedbackPayload {
    pub fn encode(&self) -> Result<Vec<u8>> {
        bincode::serialize(self).map_err(|e| ProtocolError::Serialization(e.to_string()))
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        bincode::deserialize(src).map_err(|e| ProtocolError::Deserialization(e.to_string()))
    }
}
