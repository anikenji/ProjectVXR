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
        bits_per_sample: u16,
        is_float: bool,
        is_running: bool,
        com_initialized: bool,
        audio_client: Option<IAudioClient>,
        capture_client: Option<IAudioCaptureClient>,
    }

    unsafe impl Send for WasapiLoopbackCapture {}

    impl WasapiLoopbackCapture {
        pub fn new() -> Result<Self> {
            Ok(Self {
                sample_rate: 48_000,
                channels: 2,
                bits_per_sample: 32,
                is_float: true,
                is_running: false,
                com_initialized: false,
                audio_client: None,
                capture_client: None,
            })
        }

        pub fn is_capturing_real_audio(&self) -> bool {
            self.capture_client.is_some()
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
                self.bits_per_sample = mix_format.wBitsPerSample;
                self.is_float = mix_format.wBitsPerSample == 32;

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

                let capture_client: IAudioCaptureClient = audio_client.GetService()
                    .map_err(|e| AudioError::Wasapi(format!("Failed to get IAudioCaptureClient service: {:?}", e)))?;

                audio_client.Start()
                    .map_err(|e| AudioError::Wasapi(format!("Failed to start audio client: {:?}", e)))?;

                self.audio_client = Some(audio_client);
                self.capture_client = Some(capture_client);
                self.is_running = true;

                log::info!(
                    "[VXR-Audio] Real WASAPI Loopback capture started: {} Hz, {} channels, {} bits (float={})",
                    self.sample_rate, self.channels, self.bits_per_sample, self.is_float
                );
                Ok(())
            }
        }

        fn read_samples(&mut self) -> Result<Vec<f32>> {
            if !self.is_running {
                return Ok(Vec::new());
            }

            let mut out_samples = Vec::new();

            if let Some(ref client) = self.capture_client {
                unsafe {
                    loop {
                        let next_packet_size = match client.GetNextPacketSize() {
                            Ok(size) => size,
                            Err(_) => break,
                        };

                        if next_packet_size == 0 {
                            break;
                        }

                        let mut p_data: *mut u8 = std::ptr::null_mut();
                        let mut frames_read = 0u32;
                        let mut flags = 0u32;

                        let hr = client.GetBuffer(
                            &mut p_data,
                            &mut frames_read,
                            &mut flags,
                            None,
                            None,
                        );

                        if hr.is_err() || frames_read == 0 {
                            break;
                        }

                        let channels = self.channels as usize;
                        let is_silent = (flags & (AUDCLNT_BUFFERFLAGS_SILENT.0 as u32)) != 0 || p_data.is_null();

                        if is_silent {
                            // Emit silent stereo samples for the duration
                            out_samples.resize(out_samples.len() + (frames_read as usize) * 2, 0.0);
                        } else if self.is_float {
                            // IEEE 32-bit float samples
                            let float_slice = std::slice::from_raw_parts(
                                p_data as *const f32,
                                (frames_read as usize) * channels,
                            );

                            for frame in float_slice.chunks_exact(channels) {
                                out_samples.push(frame[0]); // Left
                                out_samples.push(if channels > 1 { frame[1] } else { frame[0] }); // Right
                            }
                        } else {
                            // 16-bit integer PCM samples
                            let i16_slice = std::slice::from_raw_parts(
                                p_data as *const i16,
                                (frames_read as usize) * channels,
                            );

                            for frame in i16_slice.chunks_exact(channels) {
                                out_samples.push(frame[0] as f32 / 32768.0);
                                out_samples.push(if channels > 1 { frame[1] as f32 / 32768.0 } else { frame[0] as f32 / 32768.0 });
                            }
                        }

                        let _ = client.ReleaseBuffer(frames_read);
                    }
                }
            }

            Ok(out_samples)
        }

        fn stop(&mut self) -> Result<()> {
            if self.is_running {
                if let Some(ref client) = self.audio_client {
                    unsafe { let _ = client.Stop(); }
                }
                self.capture_client = None;
                self.audio_client = None;
                self.is_running = false;

                if self.com_initialized {
                    unsafe { CoUninitialize(); }
                    self.com_initialized = false;
                }
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
