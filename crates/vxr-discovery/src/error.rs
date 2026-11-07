use thiserror::Error;

#[derive(Error, Debug)]
pub enum DiscoveryError {
    #[error("mDNS error: {0}")]
    Mdns(String),

    #[error("Pairing failed: {0}")]
    PairingFailed(String),

    #[error("Pairing lockout: too many failed PIN attempts")]
    PairingLockout,

    #[error("Wi-Fi Direct P2P error: {0}")]
    P2p(String),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, DiscoveryError>;
