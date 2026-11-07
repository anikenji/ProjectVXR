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
        let initial_status = SystemStatus {
            is_streaming: true,
            connected_headset: Some("Meta Quest 3 (VR-Ready)".to_string()),
            gpu_name: "NVIDIA GeForce RTX 4060 Ti".to_string(),
            active_runtime: "VXR-XR Native OpenXR Runtime".to_string(),
            network_transport: "Wi-Fi 6E (160MHz Direct P2P)".to_string(),
        };

        let initial_metrics = StreamMetrics {
            fps: 90.0,
            bitrate_mbps: 150.0,
            latency: LatencyWaterfall {
                capture_ms: 1.2,
                encode_ms: 3.8,
                network_ms: 2.1,
                decode_ms: 5.4,
                total_motion_to_photon_ms: 12.5,
            },
            packets_lost_pct: 0.0,
            fec_recovered_packets: 42,
            bufferbloat_queue_ms: 0.8,
        };

        let inner = InnerState {
            status: initial_status,
            metrics: initial_metrics,
            settings: StreamerSettings::default(),
            active_pin: None,
            paired_devices: vec!["Meta Quest 3 (SN: 1WMHH81001)".to_string()],
        };

        Self {
            inner: Arc::new(RwLock::new(inner)),
        }
    }

    pub fn get_status(&self) -> SystemStatus {
        self.inner.read().unwrap().status.clone()
    }

    pub fn get_metrics(&self) -> StreamMetrics {
        self.inner.read().unwrap().metrics.clone()
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

    pub fn get_paired_devices(&self) -> Vec<String> {
        self.inner.read().unwrap().paired_devices.clone()
    }

    pub fn toggle_streaming(&self, enable: bool) -> bool {
        let mut w = self.inner.write().unwrap();
        w.status.is_streaming = enable;
        enable
    }
}
