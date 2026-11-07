# Project VXR: Hardware Video Encoder (NVENC & AMF)

`vxr-encoder` is the zero-copy GPU video encoding subsystem for Project VXR. It interfaces directly with NVIDIA NVENC (RTX 40-series Ada Lovelace / RTX 30 / RTX 20) and AMD AMF (Radeon RDNA 2/3) hardware encoders to compress Direct3D 11 / D3D12 render targets with zero CPU memory copies.

---

## Architectural Features

1. **Hardware-Accelerated Codecs**:
   - **AV1 10-Bit**: Next-generation open codec utilizing RTX 40-series dual NVENC encoders and Meta Quest 3 Snapdragon XR2 Gen 2 hardware AV1 decoders.
   - **HEVC 10-Bit (H.265 P010)**: Eliminates color banding in dark gradients and foggy VR scenes.
   - **H.264+ High Bitrate**: High profile compatible with Wi-Fi 6/6E throughputs from 400 Mbps to 500+ Mbps.

2. **Zero-Copy GPU VRAM Texture Pipeline**:
   - Registers `ID3D11Texture2D*` directly with `NvEncRegisterResource` (`NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX`).
   - Maps frames directly into NVENC hardware memory on VRAM with **0 CPU copies**.
   - Interfaces seamlessly with `vxr-capture`, `drivers/iddcx` virtual display swapchains, and `drivers/openxr` native runtime swapchains.

3. **Advanced VR Encoding Tuning**:
   - **2-Pass Encoding (`NV_ENC_TWO_PASS_QUARTER_RESOLUTION`)**: Two-pass analysis sharpens object boundaries and fine text without latency penalties.
   - **Adaptive Quantization (AQ)**: Enables both Spatial and Temporal AQ to intelligently allocate bitrate to moving focal points.
   - **Periodic Intra-Refresh & Sliced Encoding**: Splits frames into 4–8 horizontal slices and rolls intra-macroblocks over successive frames, completely eliminating IDR frame traffic spikes that cause Wi-Fi router packet jitter.
   - **Low-Latency CBR Tuning**: Ultra-low latency tuning preset (`NV_ENC_PRESET_P1` to `P4`) with sub-frame VBV buffer sizes.

4. **Dynamic In-Flight Bitrate Adaptation**:
   - Reconfigures target bitrate and framerate in real time from network congestion control feedback without session restarts.

---

## C API Reference (`vxr_encoder.h`)

```c
VxrEncoderHandle vxr_encoder_create(VxrEncoderBackend backend);
bool vxr_encoder_init(VxrEncoderHandle handle, const VxrEncoderConfig* config, void* d3d11_device);
bool vxr_encoder_encode_texture(VxrEncoderHandle handle, void* d3d11_texture, uint64_t pts_us, bool force_idr, VxrBitstreamFrame* out_frame);
bool vxr_encoder_reconfigure_bitrate(VxrEncoderHandle handle, uint32_t bitrate_bps, uint32_t fps);
void vxr_encoder_destroy(VxrEncoderHandle handle);
```

---

## Build & Test

```powershell
cmake -S vxr-encoder -B vxr-encoder/build -G "Visual Studio 16 2019" -A x64
cmake --build vxr-encoder/build --config Release
.\vxr-encoder\build\Release\test_encoder.exe
```
