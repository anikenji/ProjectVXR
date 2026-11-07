//! # VXR Server UI (Tauri v2)
//!
//! Control panel, real-time latency monitoring, settings, and pairing management.

pub mod commands;
pub mod error;
pub mod state;

pub use commands::*;
pub use error::{Result, UiError};
pub use state::{LatencyWaterfall, ServerAppState, StreamMetrics, StreamerSettings, SystemStatus};

pub fn build_app() -> tauri::Builder<tauri::Wry> {
    let app_state = ServerAppState::new();

    tauri::Builder::default()
        .manage(app_state)
        .invoke_handler(tauri::generate_handler![
            get_system_status,
            get_stream_metrics,
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
    fn test_server_app_state_defaults() {
        let state = ServerAppState::new();

        // 1. Initial System Status
        let status = state.get_status();
        assert!(status.is_streaming);
        assert_eq!(status.gpu_name, "NVIDIA GeForce RTX 4060 Ti");
        assert_eq!(status.active_runtime, "VXR-XR Native OpenXR Runtime");
        assert!(status.connected_headset.is_some());

        // 2. Metrics & Waterfall Latency
        let metrics = state.get_metrics();
        assert_eq!(metrics.fps, 90.0);
        assert_eq!(metrics.bitrate_mbps, 150.0);
        assert!(metrics.latency.total_motion_to_photon_ms < 25.0, "Total latency must be sub-25ms");
        assert!(metrics.latency.encode_ms < 5.0);
        assert!(metrics.latency.network_ms < 5.0);

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
        assert!(!state.toggle_streaming(false));
        assert!(!state.get_status().is_streaming);
        assert!(state.toggle_streaming(true));
        assert!(state.get_status().is_streaming);
    }

    #[test]
    fn test_metrics_json_serialization() {
        let state = ServerAppState::new();
        let metrics = state.get_metrics();
        let json_str = serde_json::to_string(&metrics).expect("Serialize metrics");
        assert!(json_str.contains("total_motion_to_photon_ms"));
        assert!(json_str.contains("bitrate_mbps"));

        let deserialized: StreamMetrics = serde_json::from_str(&json_str).expect("Deserialize metrics");
        assert_eq!(deserialized.fps, 90.0);
    }
}
