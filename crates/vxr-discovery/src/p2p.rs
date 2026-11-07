use std::net::Ipv4Addr;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WifiBand {
    Band5GHz,
    Band6GHz, // Wi-Fi 6E
}

#[derive(Debug, Clone)]
pub struct P2pConfig {
    pub ssid: String,
    pub passphrase: String,
    pub band: WifiBand,
    pub channel: u32,
    pub channel_width_mhz: u32,
    pub gateway_ip: Ipv4Addr,
}

impl Default for P2pConfig {
    fn default() -> Self {
        Self {
            ssid: "VXR-Quest-Direct".to_string(),
            passphrase: "vxr-direct-streaming-2026".to_string(),
            band: WifiBand::Band5GHz,
            channel: 36, // 5.180 GHz (clean non-DFS channel)
            channel_width_mhz: 160, // 160 MHz width for 2400 Mbps PHY rate
            gateway_ip: Ipv4Addr::new(192, 168, 137, 1),
        }
    }
}

#[derive(Debug, Clone)]
pub struct P2pClientInfo {
    pub mac_address: String,
    pub ip_address: Ipv4Addr,
    pub signal_percent: u8,
    pub phy_rate_mbps: u32,
}

#[derive(Debug, Clone)]
pub struct P2pStatus {
    pub is_active: bool,
    pub ssid: String,
    pub gateway_ip: Ipv4Addr,
    pub channel: u32,
    pub connected_clients: Vec<P2pClientInfo>,
}

/// Helper for configuring Windows Wi-Fi Direct P2P / SoftAP (Hosted Network)
/// to enable router-free direct streaming from PC to Meta Quest.
pub struct WifiDirectManager {
    config: P2pConfig,
    is_simulated_active: bool,
    connected_clients: Vec<P2pClientInfo>,
}

impl WifiDirectManager {
    pub fn new(config: P2pConfig) -> Self {
        Self {
            config,
            is_simulated_active: false,
            connected_clients: Vec::new(),
        }
    }

    /// Generates the Windows PowerShell / netsh command sequence to start the Hosted Network
    pub fn generate_start_commands(&self) -> Vec<String> {
        vec![
            format!(
                r#"netsh wlan set hostednetwork mode=allow ssid="{}" key="{}""#,
                self.config.ssid, self.config.passphrase
            ),
            "netsh wlan start hostednetwork".to_string(),
        ]
    }

    /// Generates the Windows command to stop the Hosted Network
    pub fn generate_stop_command(&self) -> String {
        "netsh wlan stop hostednetwork".to_string()
    }

    /// Starts P2P hosting (simulated / programmatic mode)
    pub fn start_hosting(&mut self) {
        self.is_simulated_active = true;
    }

    /// Stops P2P hosting
    pub fn stop_hosting(&mut self) {
        self.is_simulated_active = false;
        self.connected_clients.clear();
    }

    /// Simulates a Quest headset connecting directly to the P2P network
    pub fn simulate_client_connected(&mut self, mac: &str, ip: Ipv4Addr, phy_rate_mbps: u32) {
        self.connected_clients.push(P2pClientInfo {
            mac_address: mac.to_string(),
            ip_address: ip,
            signal_percent: 98,
            phy_rate_mbps,
        });
    }

    /// Returns the current P2P interface status
    pub fn get_status(&self) -> P2pStatus {
        P2pStatus {
            is_active: self.is_simulated_active,
            ssid: self.config.ssid.clone(),
            gateway_ip: self.config.gateway_ip,
            channel: self.config.channel,
            connected_clients: self.connected_clients.clone(),
        }
    }

    /// Parses output from Windows `netsh wlan show hostednetwork`
    pub fn parse_netsh_output(output: &str) -> bool {
        output.contains("Status                 : Started") || output.contains("Trạng thái             : Đã bắt đầu")
    }

    pub fn config(&self) -> &P2pConfig {
        &self.config
    }
}
