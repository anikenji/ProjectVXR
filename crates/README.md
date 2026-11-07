# Project VXR - Rust Crates

This directory contains Rust crates for the PC Streamer Host core, networking, and high-level logic:

- **vxr-protocol**: Wire protocol definition, packet serialization/deserialization, packet framing, and Reed-Solomon FEC.
- **vxr-pacer**: Micro-packet pacing engine (Token / Leaky Bucket) to avoid Wi-Fi router queue bloat, congestion control, and dynamic bitrate adaptation.
- **vxr-audio**: WASAPI loopback audio capture, Opus encoding/decoding, and audio clock drift compensation.
- **vxr-input**: Input simulation (mouse/keyboard and ViGEmBus gamepad emulation for Xbox 360 / DualShock).
- **vxr-server**: Host orchestration service, configuration management, and streaming pipeline coordinator.
