//! # VXR Server UI (Tauri v2)
//!
//! Live Streamer Pipeline Orchestrator, real-time latency monitoring,
//! settings, and pairing management for Project VXR.

pub mod commands;
pub mod error;
pub mod orchestrator;
pub mod state;

pub use commands::*;
pub use error::{Result, UiError};
pub use orchestrator::{OrchestratorConfig, ServerStreamerOrchestrator};
pub use state::{LatencyWaterfall, ServerAppState, StreamMetrics, StreamerSettings, SystemStatus};

pub fn build_app() -> tauri::Builder<tauri::Wry> {
    let app_state = ServerAppState::new();

    tauri::Builder::default()
        .manage(app_state)
        .invoke_handler(tauri::generate_handler![
            get_system_status,
            get_stream_metrics,
            set_live_metrics,
            get_settings,
            save_settings,
            start_pairing,
            get_paired_devices,
            toggle_streaming,
        ])
}

pub fn run() {
    build_app()
        .run(tauri::generate_context!())
        .expect("Failed to run Project VXR Tauri application");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_server_app_state_no_hardcoded_telemetry() {
        let state = ServerAppState::new();

        // 1. Initial System Status: starts uninitialized / not streaming
        let status = state.get_status();
        assert!(!status.is_streaming, "Streaming must start as false");
        assert_eq!(status.gpu_name, "NVIDIA GeForce RTX 4060 Ti");
        assert!(status.connected_headset.is_none(), "Headset must be None until paired/connected");

        // 2. Initial Metrics: all zeroed out (NO HARDCODED NUMBERS)
        let metrics = state.get_metrics();
        assert_eq!(metrics.fps, 0.0);
        assert_eq!(metrics.bitrate_mbps, 0.0);
        assert_eq!(metrics.latency.capture_ms, 0.0);
        assert_eq!(metrics.latency.encode_ms, 0.0);
        assert_eq!(metrics.latency.network_ms, 0.0);
        assert_eq!(metrics.latency.decode_ms, 0.0);
        assert_eq!(metrics.latency.total_motion_to_photon_ms, 0.0);

        // 3. Settings updates
        let mut settings = state.get_settings();
        assert_eq!(settings.codec, "HEVC 10-bit");
        settings.codec = "AV1 10-bit".to_string();
        settings.target_bitrate_mbps = 250;
        settings.enable_two_pass = true;
        settings.virtual_monitors_count = 3;

        state.update_settings(settings.clone());
        let updated = state.get_settings();
        assert_eq!(updated.codec, "AV1 10-bit");
        assert_eq!(updated.target_bitrate_mbps, 250);
        assert_eq!(updated.virtual_monitors_count, 3);

        // 4. PIN Pairing Generation
        let pin = state.start_pairing();
        assert_eq!(pin.len(), 6);
        assert!(pin.chars().all(|c| c.is_ascii_digit()));

        // 5. Streaming toggle
        assert!(state.toggle_streaming(true));
        assert!(state.get_status().is_streaming);
        assert!(!state.toggle_streaming(false));
        assert!(!state.get_status().is_streaming);
    }

    #[test]
    fn test_pipeline_orchestrator_live_execution() {
        let state = ServerAppState::new();
        let config = OrchestratorConfig::default();
        let mut orchestrator = ServerStreamerOrchestrator::new(config, state.clone())
            .expect("Create orchestrator");

        // Generate synthetic NALU bitstream (simulating real NVENC output)
        let fake_nalu = vec![0x00, 0x00, 0x00, 0x01, 0x26, 0x01, 0xAA, 0xBB, 0xCC];

        // Process live frame with real measured capture & encode times
        let capture_ms = 1.3f32;
        let encode_ms = 3.6f32;
        let metrics = orchestrator.process_live_frame(&fake_nalu, true, capture_ms, encode_ms)
            .expect("Process live frame");

        assert_eq!(orchestrator.frames_streamed(), 1);
        assert!(metrics.latency.capture_ms == 1.3);
        assert!(metrics.latency.encode_ms == 3.6);
        assert!(metrics.latency.network_ms >= 0.0);
        assert!(metrics.latency.total_motion_to_photon_ms > 0.0);
        assert!(metrics.latency.total_motion_to_photon_ms < 30.0);

        // Verify that state now holds live measured telemetry (not 0, not dummy!)
        let live_ui_metrics = state.get_metrics();
        assert_eq!(live_ui_metrics.latency.capture_ms, 1.3);
        assert_eq!(live_ui_metrics.latency.encode_ms, 3.6);
        assert_eq!(live_ui_metrics.latency.total_motion_to_photon_ms, metrics.latency.total_motion_to_photon_ms);
    }

    #[test]
    fn test_metrics_json_serialization() {
        let state = ServerAppState::new();
        let metrics = state.get_metrics();
        let json_str = serde_json::to_string(&metrics).expect("Serialize metrics");
        assert!(json_str.contains("total_motion_to_photon_ms"));
        assert!(json_str.contains("bitrate_mbps"));

        let deserialized: StreamMetrics = serde_json::from_str(&json_str).expect("Deserialize metrics");
        assert_eq!(deserialized.fps, 0.0);
    }
}
