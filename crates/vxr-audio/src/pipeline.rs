use crate::capture::{AudioCaptureSource, MockAudioCapture};
use crate::codec::{OpusEncoderWrapper, SAMPLES_TOTAL_5MS};
use crate::drift::{AudioClockDriftCompensator, DriftConfig};
use crate::error::Result;
use std::time::{SystemTime, UNIX_EPOCH};
use vxr_protocol::AudioPayload;

#[derive(Debug, Clone)]
pub struct AudioPipelineConfig {
    pub bitrate_bps: i32,
    pub drift_config: DriftConfig,
}

impl Default for AudioPipelineConfig {
    fn default() -> Self {
        Self {
            bitrate_bps: 128_000, // 128 kbps stereo
            drift_config: DriftConfig::default(),
        }
    }
}

pub struct AudioPipeline {
    capture: Box<dyn AudioCaptureSource>,
    drift_compensator: AudioClockDriftCompensator,
    encoder: OpusEncoderWrapper,
    accumulator: Vec<f32>,
    sequence_number: u32,
    frames_encoded: u64,
}

impl AudioPipeline {
    pub fn new(capture: Box<dyn AudioCaptureSource>, config: AudioPipelineConfig) -> Result<Self> {
        let encoder = OpusEncoderWrapper::new(config.bitrate_bps)?;
        let drift_compensator = AudioClockDriftCompensator::new(config.drift_config);

        Ok(Self {
            capture,
            drift_compensator,
            encoder,
            accumulator: Vec::with_capacity(4096),
            sequence_number: 0,
            frames_encoded: 0,
        })
    }

    /// Creates a mock test pipeline producing synthetic 48kHz 440Hz stereo audio
    pub fn new_mock(frequency: f32, config: AudioPipelineConfig) -> Result<Self> {
        let mock = Box::new(MockAudioCapture::new(frequency));
        Self::new(mock, config)
    }

    pub fn start(&mut self) -> Result<()> {
        self.capture.start()
    }

    pub fn stop(&mut self) -> Result<()> {
        self.capture.stop()
    }

    /// Ingests network feedback on client buffer level (ms) to adjust clock drift
    pub fn update_client_buffer_status(&mut self, client_buffer_ms: f32) {
        self.drift_compensator.update_client_buffer_status(client_buffer_ms);
    }

    /// Feeds raw stereo PCM float samples directly into the pipeline,
    /// applies drift compensation, frames into 5ms chunks, and returns encoded AudioPayloads.
    pub fn feed_raw_pcm(&mut self, pcm_stereo: &[f32]) -> Result<Vec<AudioPayload>> {
        // 1. Clock drift compensation
        let compensated = self.drift_compensator.process(pcm_stereo);

        // 2. Accumulate in internal buffer
        self.accumulator.extend_from_slice(&compensated);

        // 3. Extract exact 5ms frames (480 samples stereo)
        let mut payloads = Vec::new();

        while self.accumulator.len() >= SAMPLES_TOTAL_5MS {
            let chunk: Vec<f32> = self.accumulator.drain(..SAMPLES_TOTAL_5MS).collect();
            let opus_data = self.encoder.encode_float_to_vec(&chunk)?;

            let timestamp_us = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_micros() as u64)
                .unwrap_or(0);

            let payload = AudioPayload {
                sequence_number: self.sequence_number,
                timestamp_us,
                channels: 2,
                sample_rate: 48_000,
                data: opus_data,
            };

            self.sequence_number = self.sequence_number.wrapping_add(1);
            self.frames_encoded += 1;
            payloads.push(payload);
        }

        Ok(payloads)
    }

    /// Captures from underlying source, compensates drift, and produces all ready 5ms AudioPayloads.
    pub fn process_capture(&mut self) -> Result<Vec<AudioPayload>> {
        let samples = self.capture.read_samples()?;
        if samples.is_empty() {
            return Ok(Vec::new());
        }
        self.feed_raw_pcm(&samples)
    }

    pub fn frames_encoded(&self) -> u64 {
        self.frames_encoded
    }

    pub fn current_drift_ratio(&self) -> f64 {
        self.drift_compensator.current_ratio()
    }
}
