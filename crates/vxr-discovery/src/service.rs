use crate::error::Result;
use crate::mdns::{MdnsAdvertiser, MdnsServiceInfo};
use crate::p2p::{P2pConfig, P2pStatus, WifiDirectManager};
use crate::pairing::{PairedDeviceStore, PairingChallenge, PairingResponse, PairingSession};
use std::net::Ipv4Addr;

pub struct DiscoveryServiceConfig {
    pub host_name: String,
    pub ip: Ipv4Addr,
    pub port: u16,
    pub p2p_config: P2pConfig,
}

impl Default for DiscoveryServiceConfig {
    fn default() -> Self {
        Self {
            host_name: "VXR-PC-Host".to_string(),
            ip: Ipv4Addr::new(192, 168, 1, 100),
            port: 9944,
            p2p_config: P2pConfig::default(),
        }
    }
}

/// Unified local discovery, pairing, and P2P coordination service
pub struct DiscoveryService {
    config: DiscoveryServiceConfig,
    mdns_advertiser: MdnsAdvertiser,
    current_pairing_session: Option<PairingSession>,
    paired_store: PairedDeviceStore,
    p2p_manager: WifiDirectManager,
}

impl DiscoveryService {
    pub fn new(config: DiscoveryServiceConfig) -> Self {
        let service_info = MdnsServiceInfo::new(&config.host_name, config.ip, config.port);
        let mdns_advertiser = MdnsAdvertiser::new(service_info);
        let p2p_manager = WifiDirectManager::new(config.p2p_config.clone());

        Self {
            config,
            mdns_advertiser,
            current_pairing_session: None,
            paired_store: PairedDeviceStore::new(),
            p2p_manager,
        }
    }

    /// Starts mDNS service broadcasting
    pub fn start(&mut self) -> Result<()> {
        self.mdns_advertiser.start()
    }

    /// Initiates a new PIN pairing session, returning the 6-digit PIN displayed on the PC host monitor
    pub fn start_pairing(&mut self) -> String {
        let pin = PairingSession::generate_pin();
        self.current_pairing_session = Some(PairingSession::new(&pin));
        pin
    }

    /// Creates a challenge to send to the connecting Quest headset
    pub fn get_pairing_challenge(&self) -> Option<PairingChallenge> {
        self.current_pairing_session.as_ref().map(|s| s.create_challenge(&self.config.host_name))
    }

    /// Verifies client's response; on success registers client in paired store and returns auth token
    pub fn verify_pairing(&mut self, response: &PairingResponse) -> Result<String> {
        let session = self.current_pairing_session.as_mut()
            .ok_or_else(|| crate::error::DiscoveryError::PairingFailed("No active pairing session".to_string()))?;

        let token = session.verify_response(response)?;
        self.paired_store.register(&response.client_id, &response.client_name, &token);
        self.current_pairing_session = None; // Session concluded
        Ok(token)
    }

    /// Checks if a client is already paired and holds a valid auth token
    pub fn is_client_authorized(&self, client_id: &str, token: &str) -> bool {
        self.paired_store.is_authenticated(client_id, token)
    }

    pub fn start_p2p(&mut self) {
        self.p2p_manager.start_hosting();
    }

    pub fn stop_p2p(&mut self) {
        self.p2p_manager.stop_hosting();
    }

    pub fn get_p2p_status(&self) -> P2pStatus {
        self.p2p_manager.get_status()
    }

    pub fn paired_device_count(&self) -> usize {
        self.paired_store.count()
    }
}
