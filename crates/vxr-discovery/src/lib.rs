//! # VXR Discovery
//!
//! Local-first network discovery, PIN-based pairing handshake,
//! and Wi-Fi Direct P2P / SoftAP configuration helper for Project VXR.

pub mod error;
pub mod mdns;
pub mod p2p;
pub mod pairing;
pub mod service;

pub use error::{DiscoveryError, Result};
pub use mdns::{MdnsAdvertiser, MdnsBrowser, MdnsPacket, MdnsServiceInfo, VXR_SERVICE_TYPE};
pub use p2p::{P2pClientInfo, P2pConfig, P2pStatus, WifiBand, WifiDirectManager};
pub use pairing::{
    PairedClientRecord, PairedDeviceStore, PairingChallenge, PairingResponse, PairingSession,
};
pub use service::{DiscoveryService, DiscoveryServiceConfig};

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::Ipv4Addr;

    #[test]
    fn test_mdns_packet_encoding_and_parsing() {
        let ip = Ipv4Addr::new(192, 168, 1, 105);
        let service = MdnsServiceInfo::new("VXR-Desktop", ip, 9944);

        // Build binary announcement packet
        let packet_bytes = MdnsPacket::build_announcement(&service);
        assert!(!packet_bytes.is_empty());
        assert_eq!(&packet_bytes[0..2], &[0x00, 0x00]); // ID 0

        // Parse packet
        let parsed = MdnsPacket::parse_packet(&packet_bytes);
        assert!(parsed.is_some(), "Must parse valid mDNS packet");

        let p = parsed.unwrap();
        assert_eq!(p.ip, ip);
        assert_eq!(p.port, 9944);
        assert_eq!(p.txt_records.get("version"), Some(&"1.0.0".to_string()));
        assert_eq!(p.txt_records.get("status"), Some(&"available".to_string()));
        assert_eq!(p.txt_records.get("p2p"), Some(&"supported".to_string()));
    }

    #[test]
    fn test_pin_pairing_handshake_success() {
        let pin = "849201";
        let mut session = PairingSession::new(pin);
        assert_eq!(session.pin(), pin);
        assert!(!session.is_locked_out());

        let challenge = session.create_challenge("VXR-Host");

        // Client receives challenge and computes HMAC-SHA256 with entered PIN
        let client_hmac = PairingSession::compute_client_response(pin, &challenge.salt, &challenge.nonce);

        let response = PairingResponse {
            client_id: "QUEST-3-SN12345".to_string(),
            client_name: "Meta Quest 3".to_string(),
            auth_hmac: client_hmac,
        };

        // Host verifies client response
        let token_result = session.verify_response(&response);
        assert!(token_result.is_ok());
        let token = token_result.unwrap();
        assert_eq!(token.len(), 64); // 256-bit hex token

        // Verify device store
        let mut store = PairedDeviceStore::new();
        store.register(&response.client_id, &response.client_name, &token);
        assert_eq!(store.count(), 1);
        assert!(store.is_authenticated("QUEST-3-SN12345", &token));
        assert!(!store.is_authenticated("QUEST-3-SN12345", "invalid-token"));
        assert!(!store.is_authenticated("UNKNOWN-DEVICE", &token));
    }

    #[test]
    fn test_pin_pairing_brute_force_lockout() {
        let pin = "123456";
        let mut session = PairingSession::new(pin);
        let challenge = session.create_challenge("VXR-Host");

        // Wrong PIN response
        let wrong_hmac = PairingSession::compute_client_response("000000", &challenge.salt, &challenge.nonce);
        let response = PairingResponse {
            client_id: "ATTACKER".to_string(),
            client_name: "Bad Client".to_string(),
            auth_hmac: wrong_hmac,
        };

        // Attempt 1: Fails
        assert!(session.verify_response(&response).is_err());
        assert!(!session.is_locked_out());

        // Attempt 2: Fails
        assert!(session.verify_response(&response).is_err());
        assert!(!session.is_locked_out());

        // Attempt 3: Fails and locks out
        let res3 = session.verify_response(&response);
        assert!(res3.is_err());
        assert!(session.is_locked_out());

        // Attempt 4: Even with correct PIN, must be rejected due to lockout
        let correct_hmac = PairingSession::compute_client_response(pin, &challenge.salt, &challenge.nonce);
        let valid_response = PairingResponse {
            client_id: "ATTACKER".to_string(),
            client_name: "Bad Client".to_string(),
            auth_hmac: correct_hmac,
        };
        assert!(session.verify_response(&valid_response).is_err());
    }

    #[test]
    fn test_wifi_direct_p2p_config_and_status() {
        let mut config = P2pConfig::default();
        config.ssid = "VXR-Direct-VR".to_string();
        config.channel_width_mhz = 160;

        let mut p2p = WifiDirectManager::new(config.clone());
        let cmds = p2p.generate_start_commands();
        assert_eq!(cmds.len(), 2);
        assert!(cmds[0].contains(r#"mode=allow ssid="VXR-Direct-VR""#));
        assert_eq!(cmds[1], "netsh wlan start hostednetwork");

        assert!(!p2p.get_status().is_active);

        // Start hosting
        p2p.start_hosting();
        assert!(p2p.get_status().is_active);

        // Simulate Quest connection
        let quest_ip = Ipv4Addr::new(192, 168, 137, 50);
        p2p.simulate_client_connected("2C:FD:A1:00:11:22", quest_ip, 2400);

        let status = p2p.get_status();
        assert_eq!(status.connected_clients.len(), 1);
        assert_eq!(status.connected_clients[0].phy_rate_mbps, 2400);
        assert_eq!(status.connected_clients[0].ip_address, quest_ip);

        // Parse Windows netsh output helper
        let sample_output = "The hosted network status is: Status                 : Started
";
        assert!(WifiDirectManager::parse_netsh_output(sample_output));

        p2p.stop_hosting();
        assert!(!p2p.get_status().is_active);
    }

    #[test]
    fn test_discovery_service_end_to_end() {
        let config = DiscoveryServiceConfig {
            host_name: "VXR-Host-Test".to_string(),
            ip: Ipv4Addr::new(192, 168, 1, 50),
            port: 9944,
            p2p_config: P2pConfig::default(),
        };

        let mut service = DiscoveryService::new(config);
        service.start().expect("Start discovery");

        // Start PIN pairing session
        let pin = service.start_pairing();
        assert_eq!(pin.len(), 6);

        let challenge = service.get_pairing_challenge().expect("Get challenge");
        let client_hmac = PairingSession::compute_client_response(&pin, &challenge.salt, &challenge.nonce);

        let resp = PairingResponse {
            client_id: "QUEST3-DIRECT".to_string(),
            client_name: "Quest 3 Headset".to_string(),
            auth_hmac: client_hmac,
        };

        let token = service.verify_pairing(&resp).expect("Verify pairing");
        assert!(service.is_client_authorized("QUEST3-DIRECT", &token));
        assert_eq!(service.paired_device_count(), 1);

        // P2P controls
        service.start_p2p();
        assert!(service.get_p2p_status().is_active);
        service.stop_p2p();
        assert!(!service.get_p2p_status().is_active);
    }
}
