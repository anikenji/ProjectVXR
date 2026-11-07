# vxr-discovery: Local Service Discovery, PIN Pairing & Wi-Fi Direct P2P

`vxr-discovery` provides zero-configuration local networking, secure offline PIN-based pairing, and Wi-Fi Direct SoftAP configuration for Project VXR:

---

## Architectural Features

1. **Local-First & Offline Philosophy**:
   - Zero dependency on third-party cloud servers or external accounts.
   - Operates completely offline across local LAN and direct Wi-Fi links.

2. **mDNS Service Advertisement & Discovery (`MdnsAdvertiser` & `MdnsBrowser`)**:
   - Conforms to RFC 1035 / RFC 6762 zero-configuration multicasting on `224.0.0.251:5353`.
   - Advertises service `_vxr._udp.local.` with PTR, SRV, A, and TXT records.
   - Publishes host capabilities: GPU model, supported codecs (H.264, HEVC, AV1), streaming port, and P2P direct availability.
   - Automatically browses and resolves available PC streamers on the headset client.

3. **Cryptographic PIN-Based Pairing Handshake (`PairingSession`)**:
   - PC Host generates a random 6-digit numeric PIN displayed on the streamer dashboard.
   - User enters the PIN on the Quest headset.
   - **Challenge-Response Security**:
     - Host sends a random 128-bit salt and 128-bit nonce.
     - Headset computes $\text{HMAC-SHA256}(\text{PIN}, \text{salt} \parallel \text{nonce})$.
     - Host verifies the cryptographic HMAC without exposing the PIN over the wire.
   - **Persistent Auth Token**: Issues a 256-bit cryptographically random token saved in `PairedDeviceStore` for instant re-connection.
   - **Brute-Force Protection**: Enforces a strict lockout after 3 consecutive failed attempts.

4. **Wi-Fi Direct P2P & SoftAP Helper (`WifiDirectManager`)**:
   - Bypasses cluttered home Wi-Fi routers by turning the PC host Wi-Fi 6/6E card into a dedicated P2P SoftAP (Hosted Network / Mobile Hotspot).
   - Dedicated 5GHz / 6GHz 160MHz channels with up to 2400 Mbps physical PHY link rate.
   - Generates Windows shell commands (`netsh wlan`) and provides connection state, signal RSSI, and client MAC telemetry.

---

## Testing

```powershell
cargo test --manifest-path crates/vxr-discovery/Cargo.toml
```
