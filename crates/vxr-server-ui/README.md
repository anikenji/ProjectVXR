# Project VXR: Tauri v2 GUI Management Dashboard & Web Control Panel

`crates/vxr-server-ui` is the modern desktop management interface and Web control panel for Project VXR built with **Tauri v2**, **Rust**, and **Svelte / TypeScript**.

---

## Architectural Features

1. **High-Performance Tauri v2 Desktop Shell**:
   - Native Windows desktop application with negligible RAM footprint (< 40MB) compared to Electron.
   - Rust-powered backend providing thread-safe server state management, IPC command handling, and real-time telemetry streaming.

2. **Cyber/VR Dark Theme Dashboard (Svelte + TypeScript)**:
   - High-contrast visual language optimized for VR enthusiasts and flight/racing simulation streamers.
   - Real-time reactivity using Svelte stores and TypeScript typing.

3. **Live Latency Waterfall Monitoring (`MetricsDashboard.svelte`)**:
   - Visual breakdown of motion-to-photon latency stages:
     - 1. Desktop Capture (DXGI VRAM): $sim 1.2\text{ms}$
     - 2. Hardware Encoding (NVENC / AMF): $sim 3.8\text{ms}$
     - 3. Micro-Packet Network Pacing (40–60µs): $sim 2.1\text{ms}$
     - 4. Hardware Decoding + Vulkan Zero-Copy: $sim 5.4\text{ms}$
     - **Total Motion-to-Photon**: $mathbf{12.5\text{ms}}$ (well below the 20ms VR comfort threshold).
   - Real-time framerate (90 / 120 FPS), throughput (Mbps), packet loss, and Reed-Solomon FEC shard recovery counters.

4. **Hardware Video Encoder Configuration (`SettingsPanel.svelte`)**:
   - **Codec Selector**: HEVC 10-bit (P010), AV1 10-bit (RTX 40-Series Ada Lovelace), and H.264+ High Bitrate.
   - **Dynamic Bitrate Slider**: 30 Mbps to 450+ Mbps with Wi-Fi 6 / Wi-Fi 6E presets.
   - **VR Encoding Enhancements**: 2-Pass encoding, Spatial/Temporal Adaptive Quantization (AQ), and Periodic Intra-Refresh sliced encoding.

5. **IDDCX Virtual Multi-Monitor Management (`MonitorManager.svelte`)**:
   - GUI configuration of 1, 2, or 3 headless virtual monitors.
   - Resolution presets: 2064x2208 (Quest 3 native), 1080p, 1440p, 4K UHD at 90Hz / 120Hz.

6. **Cryptographic PIN Pairing Modal (`PairingModal.svelte`)**:
   - Generates and displays random 6-digit PIN codes for offline Quest headset pairing.
   - Displays authorized client tokens and active connection status.

---

## Tauri IPC Commands

```rust
get_system_status() -> SystemStatus
get_stream_metrics() -> StreamMetrics
get_settings() -> StreamerSettings
save_settings(settings: StreamerSettings) -> Result<(), String>
start_pairing() -> String (6-digit PIN)
get_paired_devices() -> Vec<String>
toggle_streaming(enable: bool) -> bool
```

---

## Build & Test

```powershell
# Run backend test suite
cargo test --manifest-path crates/vxr-server-ui/Cargo.toml

# Build full Tauri desktop bundle
cargo build --manifest-path crates/vxr-server-ui/Cargo.toml --release
```
