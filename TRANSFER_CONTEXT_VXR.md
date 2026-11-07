# PROJECT VXR: TỔNG QUAN HỆ THỐNG & CHUYỂN GIAO (BẢN CẬP NHẬT 2026)

> Dự án: Project VXR (Wireless VR/Desktop Streaming Solution)  
> Kiến trúc: Pragmatic Hybrid (Rust + C++).

## 1. PHẠM VI DỰ ÁN
Project VXR bao gồm 2 thực thể phần mềm chạy song hành:
1. **PC Streamer Host (Rust/C++)**: Thực hiện bắt frame (DXGI), mã hóa cứng (NVENC), giả lập driver (IDDCX/OpenVR).
2. **Quest Client (APK, C++/NDK)**: Native NDK, giải mã phần cứng (AMediaCodec), nội suy khung hình (SSW/SGSR).

## 2. TRIẾT LÝ PHÁT TRIỂN: "THE PRAGMATIC HYBRID"
- **Không Rust cực đoan**: Driver (C++) và Renderer (C++) phải là Native để tránh overhead/crashed ABI.
- **Tối đa hóa Rust**: Sử dụng Rust cho toàn bộ logic mạng (Pacing/FEC), Audio, Input, và GUI để đảm bảo an toàn, mượt mà (không GC) và tiết kiệm RAM.

## 3. CÁC ĐIỂM CỐT LÕI (TECHNICAL DIFFERENTIATORS)
1. **Zero-Copy Pipeline**: Frame giải mã từ `MediaCodec` -> `Vulkan Texture` không qua CPU.
2. **Micro-Packet Pacing & FEC**: Khử nghẽn router, tự phục hồi gói lỗi tức thời.
3. **Hardware Cursor 0ms**: Giả lập chuột cục bộ trên kính.
4. **QoS/DSCP**: Ưu tiên gói tin trên Wi-Fi LAN.
