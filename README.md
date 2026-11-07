# Project VXR: High-Performance Wireless VR & Desktop Streaming

Project VXR is a modern, ultra-low-latency wireless VR and desktop streaming solution connecting high-end PC hosts to standalone VR headsets (Meta Quest 2, Quest 3, Quest Pro).

Architected on a **Pragmatic Hybrid (Rust + Native C++)** philosophy, VXR combines the raw hardware performance of C++ for drivers, capture, and client rendering with the memory safety, concurrency, and high throughput of Rust for network pacing, protocol serialization, and host orchestration.

---

## Architecture Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│                          PC STREAMER HOST                              │
│                                                                        │
│  [C++ Native Subsystems - Low-level Hardware & Drivers]               │
│   ├── DXGI / Windows Graphics Capture (Zero-Copy VRAM)                 │
│   ├── NVENC / AMF Encoder (HEVC 10-bit, AV1 10-bit, H.264+, 2-Pass, AQ)│
│   ├── IDDCX Virtual Display Driver (UMDF v2 C++)                       │
│   ├── VXR-OpenVR Driver (driver_vrserver.dll)                          │
│   └── VXR-XR Native OpenXR Runtime (openxr_vxr.dll)                    │
│                          │                                             │
│                          ▼ (Zero-copy Bitstream / Ring Buffer IPC)     │
│                                                                        │
│  [Rust Core - Network, Pacing, Protocol & Control]                     │
│   ├── Token/Leaky Bucket Micro-Packet Pacer (Khử nghẽn buffer Wi-Fi)   │
│   ├── Reed-Solomon Forward Error Correction (FEC Engine)               │
│   ├── Dynamic Bitrate & Congestion Control Loop (Thích ứng mạng)       │
│   ├── Audio Engine: WASAPI Loopback + Opus + Clock Drift Compensation  │
│   ├── Input Subsystem: ViGEmBus Gamepad Emulation (Xbox 360 / DualShock│
│   ├── Security: AES-GCM Local Traffic Encryption                       │
│   └── Management UI: Tauri v2 (Rust Backend + Svelte/Web UI)           │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
                      UDP Paced Stream / DSCP 46 / Wi-Fi
                                   │
┌──────────────────────────────────▼─────────────────────────────────────┐
│                       META QUEST CLIENT (APK)                          │
│                                                                        │
│  [Pure C++ NDK + Vulkan 1.3 + Meta OpenXR SDK]                         │
│   ├── NativeActivity (Zero Java overhead on render thread)             │
│   ├── AMediaCodec Low-Latency Pipeline                                 │
│   ├── Zero-Copy Vulkan Texture Binding via AHardwareBuffer             │
│   ├── Hardware Cursor Emulation (Vẽ chuột cục bộ 0ms trên kính)        │
│   ├── OpenXR Mobile: 6DoF Tracking, Controller, SSW (SpaceWarp)        │
│   ├── Fixed Foveated Rendering (FFR via XR_FB_foveation)               │
│   └── Post-Processing: AMD FidelityFX CAS & Qualcomm SGSR Sharpening   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Monorepo Layout

```
ProjectVXR/
├── crates/             # Rust workspace: networking, protocol, audio, input, server host
│   ├── vxr-protocol/   # Packet serialization, framing, Reed-Solomon FEC
│   ├── vxr-pacer/      # Micro-packet token bucket pacer & congestion control
│   ├── vxr-audio/      # WASAPI loopback audio capture & Opus codec
│   ├── vxr-input/      # ViGEmBus controller & mouse/keyboard simulation
│   └── vxr-server/     # PC Host coordinator service
├── drivers/            # C++ Native Windows drivers & VR runtimes
│   ├── iddcx/          # Virtual Display Driver (UMDF v2)
│   ├── openvr/         # SteamVR driver (driver_vrserver.dll)
│   └── openxr/         # Native OpenXR runtime (openxr_vxr.dll)
├── quest-client/       # Meta Quest client (Pure C++ NDK, Vulkan 1.3, OpenXR Mobile)
├── Cargo.toml          # Rust workspace configuration
└── README.md           # Project documentation and specifications
```

---

## Key Technical Differentiators

1. **Zero-Copy Rendering Pipeline**:
   Frames decoded by `AMediaCodec` are bound directly to Vulkan textures via `AHardwareBuffer`, bypassing CPU memory entirely.
2. **Micro-Packet Pacing & Reed-Solomon FEC**:
   Smoothing frame bursts across the entire frame interval prevents Wi-Fi router queue bloat; integrated FEC recovers dropped packets without latency-inducing retransmission requests.
3. **Hardware Cursor 0ms**:
   Mouse cursor position is projected locally on the headset at full display refresh rate (72/90/120Hz), removing perceived latency in desktop mode.
4. **QoS / DSCP 46 (Expedited Forwarding)**:
   Network packets are tagged with DSCP 46 to ensure prioritized transmission on modern Wi-Fi 6/6E routers.
5. **Sliced Encoding & Periodic Intra-Refresh**:
   Eliminates large IDR keyframe spikes by spreading intra-macroblocks across successive frames.

---

## 6-Phase Development Roadmap

- **Phase 1: Foundation & Core Protocol**
  - Scaffold monorepo structure.
  - Implement `vxr-protocol` (packets, headers, framing, FEC).
  - Implement `vxr-capture` (DXGI desktop duplication).
  - Phase 1 QC & validation.
- **Phase 2: Video Pipeline & Encoding**
  - NVENC hardware encoding (H.264+, HEVC 10-bit, AV1).
  - Sliced encoding & intra-refresh support.
- **Phase 3: Network Transmission & Pacing**
  - Micro-packet pacing engine (Token bucket / leaky bucket).
  - Dynamic bitrate & congestion control algorithms.
- **Phase 4: Client Architecture (Quest NDK)**
  - NativeActivity C++ setup with Vulkan 1.3 and OpenXR.
  - AMediaCodec low-latency pipeline with AHardwareBuffer zero-copy.
  - Local hardware cursor (0ms).
- **Phase 5: Drivers & VR Runtimes**
  - IDDCX virtual display driver.
  - OpenVR / SteamVR integration.
  - Native VDXR-style OpenXR runtime.
- **Phase 6: Audio, Input & UI Polish**
  - WASAPI loopback + Opus audio with clock drift compensation.
  - ViGEmBus gamepad emulation.
  - Tauri v2 management dashboard.

---

## License

All rights reserved. Project VXR Development Team (2026).
