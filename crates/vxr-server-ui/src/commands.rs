use crate::state::{ServerAppState, StreamMetrics, StreamerSettings, SystemStatus};

#[tauri::command]
pub fn get_system_status(state: tauri::State<'_, ServerAppState>) -> SystemStatus {
    state.get_status()
}

#[tauri::command]
pub fn get_stream_metrics(state: tauri::State<'_, ServerAppState>) -> StreamMetrics {
    state.get_metrics()
}

#[tauri::command]
pub fn set_live_metrics(metrics: StreamMetrics, state: tauri::State<'_, ServerAppState>) -> Result<(), String> {
    state.update_live_telemetry(metrics);
    Ok(())
}

#[tauri::command]
pub fn get_settings(state: tauri::State<'_, ServerAppState>) -> StreamerSettings {
    state.get_settings()
}

#[tauri::command]
pub fn save_settings(new_settings: StreamerSettings, state: tauri::State<'_, ServerAppState>) -> Result<(), String> {
    state.update_settings(new_settings);
    Ok(())
}

#[tauri::command]
pub fn start_pairing(state: tauri::State<'_, ServerAppState>) -> String {
    state.start_pairing()
}

#[tauri::command]
pub fn get_paired_devices(state: tauri::State<'_, ServerAppState>) -> Vec<String> {
    state.get_paired_devices()
}

#[tauri::command]
pub fn toggle_streaming(enable: bool, state: tauri::State<'_, ServerAppState>) -> bool {
    state.toggle_streaming(enable)
}
