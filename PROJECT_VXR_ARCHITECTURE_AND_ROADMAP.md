# PROJECT VXR: KIẾN TRÚC TOÀN DIỆN & KẾ HOẠCH PHÁT TRIỂN (BẢN ĐẶC TẢ CHI TIẾT 2026)

> **Tài liệu đặc tả kiến trúc kỹ thuật và lộ trình thực thi dự án Project VXR (Pragmatic Hybrid)**  
> Nghiên cứu đối chiếu: Virtual Desktop v1.34 (Guy Godin), VDXR, ALVR, WiVRn, Sunshine/Moonlight.

---

## 1. MÔ HÌNH KIẾN TRÚC HYBRID (PRAGMATIC APPROACH)

Hệ thống kết hợp sức mạnh phần cứng của **C++ Native** và tính an toàn/tốc độ mạng của **Rust**:

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

## 2. BẢNG ĐẶC TẢ TÍNH NĂNG ĐỐI CHIẾU VIRTUAL DESKTOP

Dự án tích hợp đầy đủ và tối ưu hóa các tính năng tiêu chuẩn từ Virtual Desktop v1.34:

### 2.1. Video Codec & Encoding Engine
- **H.264 & H.264+**: Tương thích tối đa, hỗ trợ cấu hình Bitrate cao 400–500 Mbps cho Wi-Fi 6/6E.
- **HEVC & HEVC 10-bit (P010)**: Chuẩn nén tối ưu nhất cho Quest 2/3. 10-bit color loại bỏ triệt để hiện tượng vỡ dải màu (color banding) trong các cảnh tối/sương mù.
- **AV1 10-bit**: Khai thác phần cứng NVENC trên GPU RTX 40-series (như RTX 4060 Ti) và giải mã phần cứng trên chip Snapdragon XR2 Gen 2 (Meta Quest 3).
- **Adaptive Quantization (AQ)**: Bật Spatial & Temporal AQ trong NVENC để tối ưu phân bổ bitrate theo vùng chuyển động.
- **2-Pass Encoding**: Bật chế độ quét 2 lượt (Quarter-Res first pass) tăng độ nét biên ảnh mà không tăng đáng kể độ trễ.
- **Periodic Intra-Refresh & Sliced Encoding**: Chia khung hình thành 4–8 lát ngang và quét mới dạng cuốn chiếu, triệt tiêu hoàn toàn hiện tượng khựng hình do IDR Frame Spike khi sóng Wi-Fi dao động.

### 2.2. Tối Ưu Hiển Thị & Hiệu Năng Đồ Họa (Advanced VR Optimization)
- **Use FOV Stencil (Hidden Area Mesh)**: Khử render và nén 4 góc khuất của thấu kính VR, tiết kiệm 10–15% GPU và băng thông.
- **Horizontal & Vertical FOV Tangent Clamping**: Tùy biến thu hẹp trường nhìn để tối ưu FPS cho các tựa game mô phỏng nặng.
- **Fixed Foveated Rendering (FFR)**: Tận dụng `XR_FB_foveation` giữ nét tâm mắt và nén ngoại vi.
- **Sharpening Post-Processing**: Tích hợp shader **AMD FidelityFX CAS** và **Qualcomm SGSR** chạy trên GPU Adreno của Quest để khử mờ stream.
- **Boost Game Priority**: Tự động set `HIGH_PRIORITY_CLASS` và Windows Multimedia Class Scheduler (MMCSS) cho tiến trình game.

### 2.3. Âm Thanh, Điều Khiển & Giả Lập
- **Audio Engine**: WASAPI Loopback Capture + Opus Codec (chu kỳ 5ms). Tích hợp thuật toán **Audio Clock Drift Compensation** resample tự động theo frame rate để không bao giờ bị lệch tiếng sau thời gian dài.
- **Gamepad Emulation**: Tích hợp **ViGEmBus SDK**, tự động chuyển đổi input của Quest Controller / Bluetooth Gamepad thành tay cầm ảo Xbox 360 hoặc DualShock 4 trên PC.
- **Microphone Passthrough**: Giả lập microphone ảo trên PC để voice chat Discord / game.

### 2.4. Mạng & Độ Trễ Cực Thấp (Ultra-Low Latency Networking)
- **Token/Leaky Bucket Micro-Packet Pacing**: Bơm từng gói UDP cách nhau 40–60 microseconds, chống tràn hàng đợi buffer router (Anti-Bufferbloat).
- **Reed-Solomon FEC (Zero RTT Recovery)**: Tự sửa gói tin rớt tức thời không cần gửi yêu cầu truyền lại.
- **Dynamic Bitrate & Congestion Control Loop**: Đo lường RTT và tỷ lệ mất gói liên tục để tự động tăng giảm bitrate thích ứng.
- **QoS Socket (DSCP 46 - Expedited Forwarding)**: Gắn nhãn gói tin UDP để router Wi-Fi xếp vào hàng đợi ưu tiên cao nhất (WMM Voice/Video).
- **Hardware Cursor Emulation**: Vẽ con trỏ chuột cục bộ trên Quest ở tần số 90Hz/120Hz $\rightarrow$ **Cảm giác phản hồi chuột 0ms**.
- **Local-First & Offline**: Dò tìm LAN tự động qua mDNS / mã PIN ghép nối, không phụ thuộc tài khoản hay cloud server bên thứ ba. Tùy chọn mã hóa AES-GCM nội bộ.

---

## 3. LỘ TRÌNH PHÁT TRIỂN 6 GIAI ĐOẠN (ĐÃ TÁI CẤU TRÚC THEO MỨC ĐỘ RỦI RO)

```
[Mức 1: Foundation] Phase 1: 2D Low-Latency Streaming Loop & Hardware Cursor 0ms
       │
[Mức 2: VR Core]    Phase 2: SteamVR OpenVR Driver + 6DoF Pose Predictor + Audio/ViGEm
       │
[Mức 3: Reproject]  Phase 3: Micro-Packet Pacing, Anti-Tearing, FEC & On-Device SSW
       │
[Mức 4: Virtual HW] Phase 4: IDDCX Multi-Monitor Driver & Direct Wi-Fi P2P
       │
[Mức 5: Deep Opt]   Phase 5: VXR-XR Native OpenXR Runtime độc lập (Bypass SteamVR)
       │
[Mức 6: Expansion]  Phase 6: Spatial Canvas (Từng cửa sổ 3D), Cutout Passthrough, Linux
```

- **Phase 1 (Foundation)**: Hoàn thiện vòng lặp Desktop Capture (DXGI) $\to$ NVENC/AMF $\to$ UDP Network $\to$ Quest Decode (AMediaCodec/Vulkan) và vẽ chuột cục bộ 0ms.
- **Phase 2 (VR Core)**: Viết `driver_vrserver.dll` cho SteamVR, nhận tracking 6DoF, tích hợp WASAPI/Opus và ViGEmBus Gamepad.
- **Phase 3 (Reprojection & Quality)**: Hoàn thiện Micro-Packet Pacing, Reed-Solomon FEC, Synchronous Spacewarp (SSW) trên chip Adreno, và các bộ lọc CAS/SGSR Sharpening.
- **Phase 4 (Virtual Display)**: Viết driver màn hình ảo IDDCX (UMDF v2), hỗ trợ Wi-Fi Direct P2P.
- **Phase 5 (VXR-XR Runtime)**: Xây dựng Native OpenXR runtime độc lập để chạy trực tiếp MSFS 2024, DCS World, Assetto Corsa không qua SteamVR.
- **Phase 6 (Advanced Features)**: Bóc tách từng cửa sổ ứng dụng thành các màn hình nổi 3D (Spatial Canvas), tạo khung nhìn bàn phím thật (Desk Passthrough Cutout), và mở rộng máy chủ sang Linux.
---

## 4. KẾ HOẠCH TRIỂN KHAI PHẦN CỨNG THẬT & ĐÓNG GÓI APP QUEST (PRODUCTION ROLLOUT)

> **Mục tiêu tối thượng**: Chuyển đổi toàn bộ các tầng Mock/Simulation sang lời gọi API phần cứng thật 100%, build file APK native cho Meta Quest, kết nối không dây thực tế và chơi game PCVR trên kính vật lý.

### Cột Mốc 1: NVENC Hardware Video Encoding Thật (Physical RTX 4060 Ti)
- **Thực trạng**: Module `vxr-encoder` hiện chỉ nạp DLL `nvEncodeAPI64.dll` và sinh buffer NALU giả lập.
- **Nhiệm vụ cụ thể**:
  1. Triển khai đầy đủ con trỏ hàm `NV_ENCODE_API_FUNCTION_LIST` từ `NvEncodeAPICreateInstance`.
  2. Mở session thật `nvEncOpenEncodeSessionEx` liên kết với Direct3D 11 Device của `vxr-capture`.
  3. Cấu hình Preset Ultra Low Latency (`NV_ENC_PRESET_P1_GUID`), CBR Rate Control, Zero B-frames, Periodic Intra-Refresh.
  4. Đăng ký texture DXGI qua `nvEncRegisterResource` và map qua `nvEncMapInputResource`.
  5. Gọi `nvEncEncodePicture` thật và `nvEncLockBitstream` để trích xuất NALU bitstream HEVC 10-bit / AV1 10-bit.
  6. **Nghiệm thu thực tế**: Ghi bitstream ra file `.hevc`, phát thử bằng `ffmpeg`/`ffplay` trên máy tính để kiểm chứng hình ảnh desktop chuyển động thật.

### Cột Mốc 2: WASAPI Audio Capture Thật & Server Pipeline Hoàn Chỉnh (`vxr-server`)
- **Thực trạng**: `crates/vxr-audio` đã khởi tạo COM nhưng hàm `read_samples` trả về vec silence `0.0`. Dashboard Tauri gán cứng telemetry.
- **Nhiệm vụ cụ thể**:
  1. Gọi thật `IAudioCaptureClient::GetBuffer` để rút mẫu PCM thực tế từ card âm thanh Windows.
  2. Mã hóa Opus thật qua crate `audiopus` (khung 5ms) và chuyển sang `AudioPayload`.
  3. Xây dựng Server Orchestrator kết nối luồng: `vxr-capture` -> `vxr-encoder (NVENC)` -> `vxr-protocol (FEC)` -> `vxr-pacer` -> `VxrSocket (UDP DSCP 46)`.
  4. Gắn telemetry đo đạc thời gian thực (FPS thực tế, thời gian capture/encode GPU, ping RTT) vào Dashboard Tauri v2.

### Cột Mốc 3: Thiết lập Android NDK & Đóng gói App Meta Quest APK Thật
- **Thực trạng**: `quest-client` hiện đang build ra file DLL/EXE trên Windows với con trỏ giả lập (`0xC0DEC001`, `0x11110001`), chưa có APK và máy chưa có Android SDK/NDK.
- **Nhiệm vụ cụ thể**:
  1. Cài đặt Android SDK Platform & NDK trên phân vùng ổ `E:\Android` (tiết kiệm ổ C: hiện chỉ còn ~2GB).
  2. Biên dịch shaders GLSL 450 sang mã bytecode SPIR-V chuẩn bằng `glslc` (thay thế file 88 byte stub hiện tại).
  3. Cấu hình Gradle (`build.gradle`, `settings.gradle`, `gradlew`) và CMake để biên dịch thư viện chia sẻ `libvxr_quest.so` trên kiến trúc `arm64-v8a`.
  4. Gọi thật `AMediaCodec_createDecoderByType`, nạp buffer NALU và trích xuất `AHardwareBuffer` sang Vulkan 1.3.
  5. Tích hợp Meta OpenXR Mobile SDK lấy Head Pose 6DoF và xuất file cài đặt `vxr-quest-client.apk`.
  6. **Nghiệm thu thực tế**: Cài file APK vào kính Meta Quest thật qua `adb install`.

### Cột Mốc 4: Kết Nối E2E Không Dây Từ PC Đến Meta Quest
- **Mục tiêu**: Kính Quest mở app lên, tự tìm thấy PC qua mDNS, xác thực mã PIN 6 số, stream màn hình 90/120Hz mượt mà và điều khiển chuột 0ms.
- **Kiểm thử VR**: Tích hợp `driver_vrserver.dll` với SteamVR để chơi game PCVR bằng tay cầm Quest Touch Controller.