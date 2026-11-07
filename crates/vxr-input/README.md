# vxr-input: 6DoF Pose Predictor, Controller Mapping & ViGEm Gamepad Emulation

`vxr-input` manages tracking extrapolation, controller button/axis mapping, and Windows virtual gamepad emulation for Project VXR:

---

## Architectural Features

1. **6DoF Pose Predictor (`PosePredictor`)**:
   - Extrapolates head and controller poses forward in time (configurable lookahead, default 20ms) to counteract network transport and display presentation latency.
   - **Position Extrapolation**: Second-order kinematics: $\vec{p}(t + \Delta t) = \vec{p}(t) + \vec{v}(t)\Delta t + \frac{1}{2}\vec{a}(t)\Delta t^2$.
   - **Orientation Extrapolation**: Quaternion angular velocity integration $\Delta q \otimes q_0$ with Hamilton quaternion normalization.
   - **Acceleration Smoothing**: Outlier clamping and exponential smoothing to eliminate high-frequency jitter.

2. **Controller Input Mapping (`ControllerMapper`)**:
   - Maps Meta Quest 3 controller inputs to standard gamepad layouts:
     - Left & Right thumbsticks with radial deadbands and optional Y-axis inversion.
     - Analog triggers ($0.0 \dots 1.0 \to 0 \dots 255$).
     - Hand grips mapped to bumpers (LB / RB) at configurable squeeze thresholds.
     - Face buttons: Right A/B and Left X/Y.
     - Stick clicks (LS / RS) and menu button (Back / Select).
   - Preserves 0ms local hardware cursor state for desktop navigation.

3. **ViGEmBus Gamepad Emulation (`ViGEmManager`)**:
   - Direct integration with the **ViGEmBus** kernel driver for native virtual **Xbox 360** and **DualShock 4** gamepads.
   - Translates `VirtualGamepadReport` into kernel-level `XGamepad` / `DS4Report` packets.
   - Includes automatic `MockGamepadDevice` fallback when running in headless or CI environments without the physical driver active.

4. **Input Subsystem Coordinator (`VxrInputSubsystem`)**:
   - Single unified interface consuming raw network `vxr_protocol::InputPayload` packets and emitting:
     - Predicted 6DoF poses (ready for SteamVR OpenVR driver or OpenXR runtime).
     - Gamepad reports sent to active Windows games.
     - Hardware cursor positions.

---

## Testing

```powershell
cargo test --manifest-path crates/vxr-input/Cargo.toml
```
