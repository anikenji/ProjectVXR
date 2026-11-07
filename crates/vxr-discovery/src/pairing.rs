use hmac::{Hmac, Mac};
use rand::{Rng, RngCore};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use std::collections::HashMap;
use crate::error::{DiscoveryError, Result};

type HmacSha256 = Hmac<Sha256>;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PairingChallenge {
    pub salt: [u8; 16],
    pub nonce: [u8; 16],
    pub host_name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PairingResponse {
    pub client_id: String,
    pub client_name: String,
    pub auth_hmac: [u8; 32],
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PairedClientRecord {
    pub client_id: String,
    pub client_name: String,
    pub auth_token: String,
    pub paired_timestamp_secs: u64,
}

/// Manages PIN-based authentication and challenge-response handshakes
pub struct PairingSession {
    pin: String,
    salt: [u8; 16],
    nonce: [u8; 16],
    failed_attempts: u32,
    max_attempts: u32,
    is_locked_out: bool,
}

impl PairingSession {
    /// Creates a new pairing session with an explicit PIN
    pub fn new(pin: &str) -> Self {
        let mut rng = rand::thread_rng();
        let mut salt = [0u8; 16];
        let mut nonce = [0u8; 16];
        rng.fill_bytes(&mut salt);
        rng.fill_bytes(&mut nonce);

        Self {
            pin: pin.to_string(),
            salt,
            nonce,
            failed_attempts: 0,
            max_attempts: 3,
            is_locked_out: false,
        }
    }

    /// Generates a random 6-digit numeric PIN code (e.g. "482910")
    pub fn generate_pin() -> String {
        let mut rng = rand::thread_rng();
        let pin_val: u32 = rng.gen_range(100_000..=999_999);
        pin_val.to_string()
    }

    /// Creates a pairing challenge to transmit to the Quest client
    pub fn create_challenge(&self, host_name: &str) -> PairingChallenge {
        PairingChallenge {
            salt: self.salt,
            nonce: self.nonce,
            host_name: host_name.to_string(),
        }
    }

    /// Client helper: Computes the expected HMAC-SHA256 response given a PIN and challenge
    pub fn compute_client_response(pin: &str, salt: &[u8; 16], nonce: &[u8; 16]) -> [u8; 32] {
        let mut mac = HmacSha256::new_from_slice(pin.as_bytes()).expect("HMAC can take any key size");
        mac.update(salt);
        mac.update(nonce);
        let result = mac.finalize().into_bytes();
        let mut out = [0u8; 32];
        out.copy_from_slice(&result);
        out
    }

    /// Host verification: Verifies the client's response against the session PIN.
    /// Returns a persistent 256-bit `auth_token` hex string upon success.
    pub fn verify_response(&mut self, response: &PairingResponse) -> Result<String> {
        if self.is_locked_out {
            return Err(DiscoveryError::PairingLockout);
        }

        let expected_hmac = Self::compute_client_response(&self.pin, &self.salt, &self.nonce);

        if response.auth_hmac == expected_hmac {
            // Generate a secure persistent session token
            let mut rng = rand::thread_rng();
            let mut token_bytes = [0u8; 32];
            rng.fill_bytes(&mut token_bytes);
            let auth_token = token_bytes.iter().map(|b| format!("{:02x}", b)).collect::<String>();

            self.failed_attempts = 0;
            log::info!("[VXR-Pairing] Client '{}' successfully paired!", response.client_id);
            Ok(auth_token)
        } else {
            self.failed_attempts += 1;
            if self.failed_attempts >= self.max_attempts {
                self.is_locked_out = true;
                log::warn!("[VXR-Pairing] PIN brute-force threshold reached; session locked out.");
                return Err(DiscoveryError::PairingLockout);
            }
            Err(DiscoveryError::PairingFailed(format!(
                "Invalid PIN (attempt {} of {})",
                self.failed_attempts, self.max_attempts
            )))
        }
    }

    pub fn pin(&self) -> &str {
        &self.pin
    }

    pub fn is_locked_out(&self) -> bool {
        self.is_locked_out
    }
}

/// Registry of trusted paired devices
pub struct PairedDeviceStore {
    clients: HashMap<String, PairedClientRecord>,
}

impl Default for PairedDeviceStore {
    fn default() -> Self {
        Self::new()
    }
}

impl PairedDeviceStore {
    pub fn new() -> Self {
        Self {
            clients: HashMap::new(),
        }
    }

    pub fn register(&mut self, client_id: &str, client_name: &str, auth_token: &str) {
        self.clients.insert(
            client_id.to_string(),
            PairedClientRecord {
                client_id: client_id.to_string(),
                client_name: client_name.to_string(),
                auth_token: auth_token.to_string(),
                paired_timestamp_secs: std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_secs())
                    .unwrap_or(0),
            },
        );
    }

    pub fn is_authenticated(&self, client_id: &str, auth_token: &str) -> bool {
        if let Some(record) = self.clients.get(client_id) {
            record.auth_token == auth_token
        } else {
            false
        }
    }

    pub fn remove(&mut self, client_id: &str) -> bool {
        self.clients.remove(client_id).is_some()
    }

    pub fn count(&self) -> usize {
        self.clients.len()
    }
}
