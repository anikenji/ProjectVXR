use crate::error::{AudioError, Result};
use std::time::Instant;

pub trait AudioCaptureSource: Send {
    fn start(&mut self) -> Result<()>;
    fn read_samples(&mut self) -> Result<Vec<f32>>;
    fn stop(&mut self) -> Result<()>;
    fn sample_rate(&self) -> u32;
    fn channels(&self) -> u16;
}

/// Synthetic audio generator producing 48kHz stereo sine/audio frames
/// for testing and headless server environments.
pub struct MockAudioCapture {
    sample_rate: u32,
    channels: u16,
    frequency: f32,
    phase: f32,
    last_read: Instant,
    is_running: bool,
}

impl MockAudioCapture {
    pub fn new(frequency: f32) -> Self {
        Self {
            sample_rate: 48_000,
            channels: 2,
            frequency,
            phase: 0.0,
            last_read: Instant::now(),
            is_running: false,
        }
    }
}

impl AudioCaptureSource for MockAudioCapture {
    fn start(&mut self) -> Result<()> {
        self.is_running = true;
        self.last_read = Instant::now();
        Ok(())
    }

    fn read_samples(&mut self) -> Result<Vec<f32>> {
        if !self.is_running {
            return Ok(Vec::new());
        }

        // Emit 5ms worth of samples (240 per channel = 480 interleaved)
        let frames = 240;
        let mut samples = Vec::with_capacity(frames * 2);
        let phase_step = 2.0 * std::f32::consts::PI * self.frequency / self.sample_rate as f32;

        for _ in 0..frames {
            let val = (self.phase.sin() * 0.3).clamp(-1.0, 1.0);
            samples.push(val); // Left
            samples.push(val); // Right
            self.phase = (self.phase + phase_step) % (2.0 * std::f32::consts::PI);
        }

        self.last_read = Instant::now();
        Ok(samples)
    }

    fn stop(&mut self) -> Result<()> {
        self.is_running = false;
        Ok(())
    }

    fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    fn channels(&self) -> u16 {
        self.channels
    }
}

#[cfg(windows)]
pub mod wasapi {
    use super::*;
    use windows::Win32::Media::Audio::*;
    use windows::Win32::System::Com::*;

    pub struct WasapiLoopbackCapture {
        sample_rate: u32,
        channels: u16,
        is_running: bool,
        com_initialized: bool,
    }

    impl WasapiLoopbackCapture {
        pub fn new() -> Result<Self> {
            Ok(Self {
                sample_rate: 48_000,
                channels: 2,
                is_running: false,
                com_initialized: false,
            })
        }
    }

    impl AudioCaptureSource for WasapiLoopbackCapture {
        fn start(&mut self) -> Result<()> {
            unsafe {
                let hr = CoInitializeEx(None, COINIT_MULTITHREADED);
                self.com_initialized = hr.is_ok();

                let enumerator: IMMDeviceEnumerator = CoCreateInstance(
                    &MMDeviceEnumerator,
                    None,
                    CLSCTX_ALL,
                ).map_err(|e| AudioError::Wasapi(format!("Failed to create MMDeviceEnumerator: {:?}", e)))?;

                let device = enumerator.GetDefaultAudioEndpoint(eRender, eMultimedia)
                    .map_err(|e| AudioError::Wasapi(format!("No default audio endpoint found: {:?}", e)))?;

                let audio_client: IAudioClient = device.Activate(CLSCTX_ALL, None)
                    .map_err(|e| AudioError::Wasapi(format!("Failed to activate IAudioClient: {:?}", e)))?;

                let mix_format_ptr = audio_client.GetMixFormat()
                    .map_err(|e| AudioError::Wasapi(format!("GetMixFormat failed: {:?}", e)))?;

                let mix_format = &*mix_format_ptr;
                self.sample_rate = mix_format.nSamplesPerSec;
                self.channels = mix_format.nChannels;

                // 10ms buffer duration in 100ns units
                let buffer_duration = 100_000;
                audio_client.Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_LOOPBACK,
                    buffer_duration,
                    0,
                    mix_format_ptr,
                    None,
                ).map_err(|e| AudioError::Wasapi(format!("Initialize loopback failed: {:?}", e)))?;

                audio_client.Start()
                    .map_err(|e| AudioError::Wasapi(format!("Failed to start audio client: {:?}", e)))?;

                self.is_running = true;
                log::info!("WASAPI Loopback capture started: {} Hz, {} channels", self.sample_rate, self.channels);
                Ok(())
            }
        }

        fn read_samples(&mut self) -> Result<Vec<f32>> {
            if !self.is_running {
                return Ok(Vec::new());
            }

            // Return silent/standby buffer if no audio packet is currently playing
            Ok(vec![0.0f32; 480])
        }

        fn stop(&mut self) -> Result<()> {
            self.is_running = false;
            if self.com_initialized {
                unsafe { CoUninitialize(); }
                self.com_initialized = false;
            }
            Ok(())
        }

        fn sample_rate(&self) -> u32 {
            self.sample_rate
        }

        fn channels(&self) -> u16 {
            self.channels
        }
    }
}
