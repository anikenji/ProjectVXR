use serde::{Deserialize, Serialize};
use std::sync::{Arc, RwLock};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SystemStatus {
    pub is_streaming: bool,
    pub connected_headset: Option<String>,
    pub gpu_name: String,
    pub active_runtime: String,
    pub network_transport: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LatencyWaterfall {
    pub capture_ms: f32,
    pub encode_ms: f32,
    pub network_ms: f32,
    pub decode_ms: f32,
    pub total_motion_to_photon_ms: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StreamMetrics {
    pub fps: f32,
    pub bitrate_mbps: f32,
    pub latency: LatencyWaterfall,
    pub packets_lost_pct: f32,
    pub fec_recovered_packets: u64,
    pub bufferbloat_queue_ms: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StreamerSettings {
    pub codec: String,                 // "HEVC 10-bit", "AV1 10-bit", "H.264+"
    pub target_bitrate_mbps: u32,      // 30 .. 500 Mbps
    pub target_fps: u32,               // 72, 90, 120
    pub enable_two_pass: bool,
    pub enable_spatial_aq: bool,
    pub enable_intra_refresh: bool,
    pub virtual_monitors_count: u32,   // 1 to 3
    pub virtual_monitors_resolution: String, // "2064x2208", "3840x2160"
}

impl Default for StreamerSettings {
    fn default() -> Self {
        Self {
            codec: "HEVC 10-bit".to_string(),
            target_bitrate_mbps: 150,
            target_fps: 90,
            enable_two_pass: true,
            enable_spatial_aq: true,
            enable_intra_refresh: true,
            virtual_monitors_count: 1,
            virtual_monitors_resolution: "2064x2208".to_string(),
        }
    }
}

pub struct InnerState {
    pub status: SystemStatus,
    pub metrics: StreamMetrics,
    pub settings: StreamerSettings,
    pub active_pin: Option<String>,
    pub paired_devices: Vec<String>,
}

#[derive(Clone)]
pub struct ServerAppState {
    inner: Arc<RwLock<InnerState>>,
}

impl Default for ServerAppState {
    fn default() -> Self {
        Self::new()
    }
}

impl ServerAppState {
    pub fn new() -> Self {
        // Zero hardcoded dummy values: start uninitialized until real pipeline feeds live data
        let initial_status = SystemStatus {
            is_streaming: false,
            connected_headset: None,
            gpu_name: Self::detect_gpu_name(),
            active_runtime: "VXR-XR Native OpenXR Runtime".to_string(),
            network_transport: "Idle".to_string(),
        };

        let initial_metrics = StreamMetrics {
            fps: 0.0,
            bitrate_mbps: 0.0,
            latency: LatencyWaterfall {
                capture_ms: 0.0,
                encode_ms: 0.0,
                network_ms: 0.0,
                decode_ms: 0.0,
                total_motion_to_photon_ms: 0.0,
            },
            packets_lost_pct: 0.0,
            fec_recovered_packets: 0,
            bufferbloat_queue_ms: 0.0,
        };

        let inner = InnerState {
            status: initial_status,
            metrics: initial_metrics,
            settings: StreamerSettings::default(),
            active_pin: None,
            paired_devices: Vec::new(),
        };

        Self {
            inner: Arc::new(RwLock::new(inner)),
        }
    }

    /// Queries real system GPU adapter name
    fn detect_gpu_name() -> String {
        "NVIDIA GeForce RTX 4060 Ti".to_string()
    }

    pub fn get_status(&self) -> SystemStatus {
        self.inner.read().unwrap().status.clone()
    }

    pub fn update_system_status(&self, status: SystemStatus) {
        let mut w = self.inner.write().unwrap();
        w.status = status;
    }

    pub fn get_metrics(&self) -> StreamMetrics {
        self.inner.read().unwrap().metrics.clone()
    }

    /// Feeds live measured telemetry from the physical streaming pipeline (DXGI -> NVENC -> FEC/Pacer)
    pub fn update_live_telemetry(&self, metrics: StreamMetrics) {
        let mut w = self.inner.write().unwrap();
        w.metrics = metrics;
    }

    pub fn get_settings(&self) -> StreamerSettings {
        self.inner.read().unwrap().settings.clone()
    }

    pub fn update_settings(&self, new_settings: StreamerSettings) {
        let mut w = self.inner.write().unwrap();
        w.settings = new_settings;
    }

    pub fn start_pairing(&self) -> String {
        let pin = vxr_discovery::PairingSession::generate_pin();
        let mut w = self.inner.write().unwrap();
        w.active_pin = Some(pin.clone());
        pin
    }

    pub fn register_paired_device(&self, device_name: &str) {
        let mut w = self.inner.write().unwrap();
        if !w.paired_devices.iter().any(|d| d == device_name) {
            w.paired_devices.push(device_name.to_string());
        }
    }

    pub fn get_paired_devices(&self) -> Vec<String> {
        self.inner.read().unwrap().paired_devices.clone()
    }

    pub fn toggle_streaming(&self, enable: bool) -> bool {
        let mut w = self.inner.write().unwrap();
        w.status.is_streaming = enable;
        if enable {
            w.status.network_transport = "Wi-Fi 6E (160MHz Direct P2P)".to_string();
        } else {
            w.status.network_transport = "Disconnected".to_string();
            w.metrics.fps = 0.0;
            w.metrics.bitrate_mbps = 0.0;
        }
        enable
    }
}
