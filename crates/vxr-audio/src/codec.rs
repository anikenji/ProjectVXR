use audiopus::{coder::{Decoder, Encoder}, Application, Bitrate, Channels, SampleRate};
use crate::error::{AudioError, Result};

pub const OPUS_SAMPLE_RATE: u32 = 48_000;
pub const OPUS_CHANNELS: usize = 2;
pub const OPUS_FRAME_MS: f32 = 5.0;

/// Number of audio samples per channel in a 5ms frame at 48kHz
pub const SAMPLES_PER_CHANNEL_5MS: usize = 240; // 48000 * 0.005 = 240

/// Total interleaved stereo samples in a 5ms frame
pub const SAMPLES_TOTAL_5MS: usize = SAMPLES_PER_CHANNEL_5MS * OPUS_CHANNELS; // 480

/// Maximum size of an encoded Opus frame buffer
pub const MAX_OPUS_PACKET_SIZE: usize = 1275;

/// High-performance Opus encoder configured for 5ms ultra-low latency VR streaming
pub struct OpusEncoderWrapper {
    encoder: Encoder,
    bitrate_bps: i32,
}

impl OpusEncoderWrapper {
    pub fn new(bitrate_bps: i32) -> Result<Self> {
        let mut encoder = Encoder::new(
            SampleRate::Hz48000,
            Channels::Stereo,
            Application::LowDelay,
        ).map_err(|e| AudioError::Opus(format!("Failed to create Opus encoder: {:?}", e)))?;

        encoder.set_bitrate(Bitrate::BitsPerSecond(bitrate_bps))
            .map_err(|e| AudioError::Opus(format!("Failed to set Opus bitrate: {:?}", e)))?;

        Ok(Self {
            encoder,
            bitrate_bps,
        })
    }

    /// Encodes a 5ms chunk (480 interleaved f32 samples) into the provided destination buffer.
    /// Returns the number of compressed bytes written.
    pub fn encode_float(&mut self, pcm_stereo: &[f32], out_packet: &mut [u8]) -> Result<usize> {
        if pcm_stereo.len() != SAMPLES_TOTAL_5MS {
            return Err(AudioError::Buffer(format!(
                "Expected {} samples (5ms stereo), got {}",
                SAMPLES_TOTAL_5MS,
                pcm_stereo.len()
            )));
        }

        self.encoder
            .encode_float(pcm_stereo, out_packet)
            .map_err(|e| AudioError::Opus(format!("Opus encode_float failed: {:?}", e)))
    }

    /// Encodes a 5ms chunk into a newly allocated `Vec<u8>`.
    pub fn encode_float_to_vec(&mut self, pcm_stereo: &[f32]) -> Result<Vec<u8>> {
        let mut buf = [0u8; MAX_OPUS_PACKET_SIZE];
        let size = self.encode_float(pcm_stereo, &mut buf)?;
        Ok(buf[..size].to_vec())
    }

    pub fn set_bitrate(&mut self, bitrate_bps: i32) -> Result<()> {
        self.encoder.set_bitrate(Bitrate::BitsPerSecond(bitrate_bps))
            .map_err(|e| AudioError::Opus(format!("Failed to set Opus bitrate: {:?}", e)))?;
        self.bitrate_bps = bitrate_bps;
        Ok(())
    }

    pub fn bitrate(&self) -> i32 {
        self.bitrate_bps
    }
}

/// Opus decoder to decode received Opus packets back to 5ms PCM frames
pub struct OpusDecoderWrapper {
    decoder: Decoder,
}

impl OpusDecoderWrapper {
    pub fn new() -> Result<Self> {
        let decoder = Decoder::new(SampleRate::Hz48000, Channels::Stereo)
            .map_err(|e| AudioError::Opus(format!("Failed to create Opus decoder: {:?}", e)))?;

        Ok(Self { decoder })
    }

    /// Decodes an Opus packet into the provided destination PCM buffer.
    /// If `packet` is None, performs packet loss concealment (PLC).
    /// Destination buffer should have at least 480 floats for a 5ms frame.
    pub fn decode_float(&mut self, packet: Option<&[u8]>, out_pcm_stereo: &mut [f32]) -> Result<usize> {
        if out_pcm_stereo.len() < SAMPLES_TOTAL_5MS {
            return Err(AudioError::Buffer(format!(
                "Destination buffer too small: {} < {}",
                out_pcm_stereo.len(),
                SAMPLES_TOTAL_5MS
            )));
        }

        self.decoder
            .decode_float(packet, out_pcm_stereo, false)
            .map_err(|e| AudioError::Opus(format!("Opus decode_float failed: {:?}", e)))
    }
}
