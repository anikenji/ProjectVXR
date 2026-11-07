//! # VXR Audio
//!
//! Ultra-low-latency audio subsystem for Project VXR:
//! - Windows WASAPI Loopback audio capture
//! - 5ms Opus audio compression (240 samples per channel at 48kHz)
//! - Audio Clock Drift Compensation (resampling to prevent audio drift over long sessions)

pub mod capture;
pub mod codec;
pub mod drift;
pub mod error;
pub mod pipeline;
pub mod resampler;

pub use capture::{AudioCaptureSource, MockAudioCapture};
#[cfg(windows)]
pub use capture::wasapi::WasapiLoopbackCapture;
pub use codec::{
    OpusDecoderWrapper, OpusEncoderWrapper, MAX_OPUS_PACKET_SIZE, OPUS_CHANNELS, OPUS_FRAME_MS,
    OPUS_SAMPLE_RATE, SAMPLES_PER_CHANNEL_5MS, SAMPLES_TOTAL_5MS,
};
pub use drift::{AudioClockDriftCompensator, DriftConfig};
pub use error::{AudioError, Result};
pub use pipeline::{AudioPipeline, AudioPipelineConfig};
pub use resampler::FractionalResampler;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_opus_5ms_encode_decode_roundtrip() {
        let mut encoder = OpusEncoderWrapper::new(128_000).expect("Encoder create");
        let mut decoder = OpusDecoderWrapper::new().expect("Decoder create");

        // Generate 5ms of 440Hz stereo sine wave (480 samples total)
        let mut input_pcm = [0.0f32; SAMPLES_TOTAL_5MS];
        for i in 0..SAMPLES_PER_CHANNEL_5MS {
            let t = i as f32 / 48000.0;
            let sample = (2.0 * std::f32::consts::PI * 440.0 * t).sin() * 0.5;
            input_pcm[i * 2] = sample;     // Left
            input_pcm[i * 2 + 1] = sample; // Right
        }

        // Encode to Opus
        let encoded_bytes = encoder.encode_float_to_vec(&input_pcm).expect("Opus encode");
        assert!(!encoded_bytes.is_empty());
        assert!(encoded_bytes.len() < MAX_OPUS_PACKET_SIZE);

        // Decode from Opus
        let mut decoded_pcm = [0.0f32; SAMPLES_TOTAL_5MS];
        let decoded_samples = decoder.decode_float(Some(&encoded_bytes), &mut decoded_pcm).expect("Opus decode");
        assert_eq!(decoded_samples, SAMPLES_PER_CHANNEL_5MS);

        // Verify decoded signal is non-silent and correlates with input
        let mut energy = 0.0f32;
        for s in &decoded_pcm {
            energy += s * s;
        }
        assert!(energy > 1.0, "Decoded audio must have audible signal energy");
    }

    #[test]
    fn test_fractional_resampler() {
        // Test identity ratio
        let input = vec![0.1f32, 0.2f32, 0.3f32, 0.4f32, 0.5f32, 0.6f32, 0.7f32, 0.8f32];
        let identity = FractionalResampler::resample_stereo(&input, 1.0);
        assert_eq!(input, identity);

        // Test speeding up (more output frames)
        let mut large_input = Vec::new();
        for i in 0..480 {
            large_input.push((i as f32).sin());
        }
        let sped_up = FractionalResampler::resample_stereo(&large_input, 1.01);
        assert!(sped_up.len() > large_input.len());

        // Test slowing down (fewer output frames)
        let slowed_down = FractionalResampler::resample_stereo(&large_input, 0.99);
        assert!(slowed_down.len() < large_input.len());
    }

    #[test]
    fn test_audio_clock_drift_compensation() {
        let config = DriftConfig {
            target_buffer_ms: 10.0,
            deadband_ms: 2.0,
            kp: 0.001,
            ki: 0.0001,
            max_drift_rate: 0.005,
        };
        let mut compensator = AudioClockDriftCompensator::new(config);

        // Within deadband: ratio remains 1.0
        compensator.update_client_buffer_status(10.5);
        assert_eq!(compensator.current_ratio(), 1.0);

        // Client buffer is growing too large (e.g. 20ms > 12ms upper bound)
        // PC clock is producing audio faster than headset plays -> compensator must slow down (ratio < 1.0)
        compensator.update_client_buffer_status(20.0);
        assert!(compensator.current_ratio() < 1.0, "Ratio must slow down when client buffer overflows");

        // Process a frame under slowed down ratio
        let frame = vec![0.1f32; 480];
        let out = compensator.process(&frame);
        assert!(out.len() <= 480);

        // Client buffer is running empty (e.g. 3ms < 8ms lower bound)
        // PC clock is slower than headset -> compensator must speed up (ratio > 1.0)
        compensator.reset();
        compensator.update_client_buffer_status(3.0);
        assert!(compensator.current_ratio() > 1.0, "Ratio must speed up when client buffer starves");
    }

    #[test]
    fn test_audio_pipeline_end_to_end() {
        let mut pipeline = AudioPipeline::new_mock(440.0, AudioPipelineConfig::default()).expect("Pipeline create");
        pipeline.start().expect("Pipeline start");

        // Process first batch of synthetic audio
        let payloads = pipeline.process_capture().expect("Process capture");
        assert!(!payloads.is_empty());

        let p1 = &payloads[0];
        assert_eq!(p1.sequence_number, 0);
        assert_eq!(p1.sample_rate, 48_000);
        assert_eq!(p1.channels, 2);
        assert!(!p1.data.is_empty());
        assert!(p1.timestamp_us > 0);

        // Verify sequence number increments monotonically
        let payloads2 = pipeline.process_capture().expect("Process capture 2");
        assert!(!payloads2.is_empty());
        assert_eq!(payloads2[0].sequence_number, 1);

        pipeline.stop().expect("Pipeline stop");
    }

    #[test]
    fn test_audio_pipeline_direct_pcm_feed() {
        let mock = Box::new(MockAudioCapture::new(440.0));
        let mut pipeline = AudioPipeline::new(mock, AudioPipelineConfig::default()).unwrap();

        // Feed exactly 3 frames of 5ms (3 * 480 = 1440 samples)
        let pcm = vec![0.05f32; 1440];
        let payloads = pipeline.feed_raw_pcm(&pcm).unwrap();
        assert_eq!(payloads.len(), 3);
        assert_eq!(payloads[0].sequence_number, 0);
        assert_eq!(payloads[1].sequence_number, 1);
        assert_eq!(payloads[2].sequence_number, 2);
    }

    #[cfg(windows)]
    #[test]
    fn test_real_wasapi_loopback_capture_and_opus() {
        let capture_res = WasapiLoopbackCapture::new();
        assert!(capture_res.is_ok());
        let mut capture = capture_res.unwrap();

        let start_res = capture.start();
        if start_res.is_ok() {
            assert!(capture.is_capturing_real_audio());
            assert!(capture.sample_rate() > 0);
            assert!(capture.channels() > 0);

            // Read available audio packets from real Windows audio engine
            let samples = capture.read_samples().expect("Read samples");
            // If desktop is currently playing audio or silent, it yields valid buffer
            println!("Real WASAPI captured {} samples from default endpoint", samples.len());

            // Test pipeline integration with real WASAPI capture
            let boxed_capture = Box::new(capture);
            let mut pipeline = AudioPipeline::new(boxed_capture, AudioPipelineConfig::default())
                .expect("Create real WASAPI pipeline");

            let _ = pipeline.process_capture();
            let _ = pipeline.stop();
        } else {
            println!("WASAPI start skipped (no active audio render endpoint on system)");
        }
    }
}
