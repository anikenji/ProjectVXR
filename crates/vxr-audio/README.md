# vxr-audio: Ultra-Low-Latency Audio Subsystem for Project VXR

`vxr-audio` handles high-fidelity audio capture, low-latency compression, and synchronization for Project VXR:

---

## Architectural Features

1. **Windows WASAPI Loopback Audio Capture**:
   - Captures default desktop/game audio from Windows endpoints using `AUDCLNT_STREAMFLAGS_LOOPBACK`.
   - Intercepts 48kHz stereo floating-point audio with silence padding when desktop audio is quiet.

2. **5ms Low-Latency Opus Compression**:
   - Configured with Opus RFC 6716 `Application::LowDelay` at 48,000 Hz stereo.
   - Operates on precise **5ms frame boundaries** (240 samples per channel = 480 interleaved float samples).
   - Minimizes packetization latency down to 5ms (compared to standard 20ms in consumer VoIP).
   - Encodes to and decodes from compressed byte streams packaged directly into `vxr_protocol::AudioPayload`.

3. **Audio Clock Drift Compensation**:
   - Compensates for clock drift between independent hardware crystal oscillators on the PC soundcard and Quest headset DAC (which typically drift by 20–60 PPM).
   - Ingests real-time client buffer queue telemetry from `FeedbackPayload`.
   - Employs a PI (Proportional-Integral) feedback controller with a 2ms deadband to dynamically modulate the effective resampling ratio within $\pm 0.5\%$.
   - Uses high-quality **cubic Hermite spline fractional interpolation** for seamless, click-free pitch and sample rate micro-adjustments.

4. **Complete Audio Streaming Pipeline**:
   - `AudioPipeline`: End-to-end integration combining capture, drift compensation, 5ms framing, Opus compression, and sequenced timestamped payload emission.

---

## Testing

```powershell
cargo test --manifest-path crates/vxr-audio/Cargo.toml
```
