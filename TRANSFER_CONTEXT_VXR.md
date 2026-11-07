# PROJECT VXR: TỔNG QUAN HỆ THỐNG & KẾ HOẠCH HOÀN THIỆN THỰC CHIẾN (PRODUCTION ROLLOUT)

> **Tài liệu chuyển giao kỹ thuật & Kế hoạch triển khai phần cứng thật**  
> Dự án: Project VXR (Wireless VR/Desktop Streaming Solution)  
> Bối cảnh: Đã hoàn tất 6 Phase Thiết kế kiến trúc & Mock Validation. Bước vào Giai đoạn Hoàn thiện Phần cứng Thật (Production Rollout).

---

## 1. THỰC TRẠNG DỰ ÁN & PHÂN ĐỊNH RANH GIỚI KIỂM THỬ

| Hạng mục | Trạng thái hiện tại | Yêu cầu hoàn thiện thật |
| :--- | :--- | :--- |
| **Giao thức mạng (vxr-protocol)** | ✅ Code thật 100% (Đóng gói nhị phân, Reed-Solomon FEC, UDP DSCP 46). | Duy trì, tích hợp vào server pipeline. |
| **Điều tốc (vxr-pacer)** | ✅ Code thật 100% (Token Bucket micro-packet pacing 40-60µs, AIMD). | Kết nối dữ liệu phản hồi RTT/Loss thực tế từ kính. |
| **Bắt hình (vxr-capture)** | ✅ Code thật 100% (Direct3D 11 & DXGI Output Duplication trên GPU). | Xuất Texture trực tiếp sang NVENC. |
| **Mã hóa (vxr-encoder)** | ⚠️ Mock Header NALU (đã nạp nvEncodeAPI64.dll nhưng chưa nén thật). | **Bắt buộc**: Gọi `nvEncEncodePicture` & `nvEncLockBitstream` thật trên RTX 4060 Ti. |
| **Âm thanh (vxr-audio)** | ⚠️ Đã khởi tạo WASAPI nhưng `read_samples` trả về vec rỗng/silence. | **Bắt buộc**: Đọc PCM thật qua `IAudioCaptureClient::GetBuffer`. |
| **Server Ghép nối (Orchestrator)**| ⚠️ Chưa nối luồng, Dashboard hiển thị telemetry gán cứng. | **Bắt buộc**: Xây dựng tiến trình stream hoàn chỉnh đo đạc FPS/Latency thật. |
| **Quest Client (APK)** | ⚠️ Build dạng DLL/EXE trên Windows với con trỏ giả lập. | **Bắt buộc**: Cấu hình Gradle NDK, gọi `AMediaCodec` thật, build ra file `.apk` cài lên Quest. |

---

## 2. LỘ TRÌNH 4 CỘT MỐC HOÀN THIỆN THỰC CHIẾN (PRODUCTION MILESTONES)

```
[CỘT MỐC A] NVENC Hardware Encoding Thật trên RTX 4060 Ti
       │ (Mở session thật, nén D3D11 Texture ra HEVC/AV1 thật, kiểm chứng qua ffplay)
       ▼
[CỘT MỐC B] Tiến trình Server Hoàn chỉnh & WASAPI Loopback Thật
       │ (Capture -> NVENC -> FEC -> Pacer -> UDP; Gắn telemetry thật vào Dashboard)
       ▼
[CỘT MỐC C] Quest Client Android NDK Thật & Đóng gói APK
       │ (Cài NDK trên ổ E:, compile SPIR-V shaders thật, AMediaCodec zero-copy, build APK)
       ▼
[CỘT MỐC D] Kết nối E2E Không dây Kính Quest - PC & Chơi Game Thật
       │ (Ghép PIN mDNS, stream 90Hz/120Hz mượt mà, truyền tracking 6DoF vào SteamVR)
```
