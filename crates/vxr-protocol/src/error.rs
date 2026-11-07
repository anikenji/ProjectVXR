use thiserror::Error;

#[derive(Error, Debug)]
pub enum ProtocolError {
    #[error("Packet buffer too short: expected at least {expected} bytes, got {actual}")]
    BufferTooShort { expected: usize, actual: usize },

    #[error("Invalid protocol magic: expected 0x{expected:08X}, got 0x{actual:08X}")]
    InvalidMagic { expected: u32, actual: u32 },

    #[error("Unsupported protocol version: {0}")]
    UnsupportedVersion(u8),

    #[error("Invalid packet type: {0}")]
    InvalidPacketType(u8),

    #[error("Payload length mismatch: header specifies {header_len} bytes, buffer has {actual_len}")]
    PayloadLengthMismatch { header_len: usize, actual_len: usize },

    #[error("Serialization error: {0}")]
    Serialization(String),

    #[error("Deserialization error: {0}")]
    Deserialization(String),

    #[error("FEC error: {0}")]
    Fec(String),

    #[error("Socket I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Packet exceeds MTU: size {size} > MTU {mtu}")]
    PacketTooLarge { size: usize, mtu: usize },

    #[error("Invalid FEC shard configuration: data_shards={data}, parity_shards={parity}")]
    InvalidFecConfig { data: usize, parity: usize },
}

pub type Result<T> = std::result::Result<T, ProtocolError>;
