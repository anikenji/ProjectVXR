# vxr-pacer: Micro-Packet Token Bucket Pacing & Congestion Control

`vxr-pacer` is the network transmission smoothing and dynamic congestion adaptation engine for Project VXR.

---

## Architectural Features

1. **Token / Leaky Bucket Rate Limiting (`TokenBucket`)**:
   - Smooths frame bursts (e.g. 50–150 UDP packets per 50–200 KB video frame) across the frame interval.
   - Prevents router buffer bloat and sudden packet drops on consumer Wi-Fi 6/6E routers.

2. **Micro-Packet Pacing (`HybridSleeper` & `PacketPacer`)**:
   - Enforces 40–60 microsecond inter-packet spacing using sub-millisecond hybrid spin-sleep timing.
   - **Traffic Prioritization**: Audio, Input (6DoF pose), and Control packets bypass pacing delays and are transmitted immediately to preserve real-time responsiveness.
   - Regulates video frame fragments and Reed-Solomon FEC parity shards.

3. **Dynamic Bitrate & Congestion Control Loop (`CongestionController`)**:
   - Driven by continuous RTT measurements and FEC packet loss telemetry reported in `FeedbackPayload`.
   - **Bufferbloat Detection**: Tracks both smoothed RTT and minimum base RTT to calculate queuing delay ($\text{SRTT} - \text{min\_RTT}$).
   - **Adaptive Rate Control**:
     - *Congested / Delay Spike*: Multiplicative decrease ($-15\%$ step) to quickly drain network queues.
     - *Holding*: Maintains steady rate when queue delay approaches threshold.
     - *Clean*: Additive increase ($+3$ Mbps step) up to max capacity (e.g. 400–500 Mbps on Wi-Fi 6E).

---

## Testing

```powershell
cargo test --manifest-path crates/vxr-pacer/Cargo.toml
```
