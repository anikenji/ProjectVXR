use byteorder::{ByteOrder, LittleEndian};
use serde::{Deserialize, Serialize};
use crate::error::{ProtocolError, Result};

pub const PROTOCOL_MAGIC: u32 = 0x50525856; // ASCII "VXRP" in Little Endian
pub const PROTOCOL_VERSION: u8 = 1;
pub const HEADER_SIZE: usize = 24;
pub const DEFAULT_MAX_MTU: usize = 1400;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[repr(u8)]
pub enum PacketType {
    Control = 0x01,
    Video = 0x02,
    Audio = 0x03,
    Input = 0x04,
    Feedback = 0x05,
    Fec = 0x06,
}

impl PacketType {
    pub fn from_u8(val: u8) -> Result<Self> {
        match val {
            0x01 => Ok(PacketType::Control),
            0x02 => Ok(PacketType::Video),
            0x03 => Ok(PacketType::Audio),
            0x04 => Ok(PacketType::Input),
            0x05 => Ok(PacketType::Feedback),
            0x06 => Ok(PacketType::Fec),
            _ => Err(ProtocolError::InvalidPacketType(val)),
        }
    }
}

pub mod flags {
    pub const NONE: u16 = 0x0000;
    pub const RELIABLE: u16 = 0x0001;
    pub const FRAGMENTED: u16 = 0x0002;
    pub const KEYFRAME: u16 = 0x0004;
    pub const FEC_PROTECTED: u16 = 0x0008;
    pub const LAST_FRAGMENT: u16 = 0x0010;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Header {
    pub magic: u32,
    pub version: u8,
    pub packet_type: PacketType,
    pub flags: u16,
    pub sequence: u32,
    pub payload_len: u16,
    pub reserved: u16,
    pub timestamp_us: u64,
}

impl Header {
    pub fn new(packet_type: PacketType, sequence: u32, flags: u16, timestamp_us: u64, payload_len: u16) -> Self {
        Self {
            magic: PROTOCOL_MAGIC,
            version: PROTOCOL_VERSION,
            packet_type,
            flags,
            sequence,
            payload_len,
            reserved: 0,
            timestamp_us,
        }
    }

    pub fn encode(&self, dst: &mut [u8]) -> Result<()> {
        if dst.len() < HEADER_SIZE {
            return Err(ProtocolError::BufferTooShort {
                expected: HEADER_SIZE,
                actual: dst.len(),
            });
        }

        LittleEndian::write_u32(&mut dst[0..4], self.magic);
        dst[4] = self.version;
        dst[5] = self.packet_type as u8;
        LittleEndian::write_u16(&mut dst[6..8], self.flags);
        LittleEndian::write_u32(&mut dst[8..12], self.sequence);
        LittleEndian::write_u16(&mut dst[12..14], self.payload_len);
        LittleEndian::write_u16(&mut dst[14..16], self.reserved);
        LittleEndian::write_u64(&mut dst[16..24], self.timestamp_us);
        Ok(())
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        if src.len() < HEADER_SIZE {
            return Err(ProtocolError::BufferTooShort {
                expected: HEADER_SIZE,
                actual: src.len(),
            });
        }

        let magic = LittleEndian::read_u32(&src[0..4]);
        if magic != PROTOCOL_MAGIC {
            return Err(ProtocolError::InvalidMagic {
                expected: PROTOCOL_MAGIC,
                actual: magic,
            });
        }

        let version = src[4];
        if version != PROTOCOL_VERSION {
            return Err(ProtocolError::UnsupportedVersion(version));
        }

        let packet_type = PacketType::from_u8(src[5])?;
        let flags = LittleEndian::read_u16(&src[6..8]);
        let sequence = LittleEndian::read_u32(&src[8..12]);
        let payload_len = LittleEndian::read_u16(&src[12..14]);
        let reserved = LittleEndian::read_u16(&src[14..16]);
        let timestamp_us = LittleEndian::read_u64(&src[16..24]);

        Ok(Self {
            magic,
            version,
            packet_type,
            flags,
            sequence,
            payload_len,
            reserved,
            timestamp_us,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Packet {
    pub header: Header,
    pub payload: Vec<u8>,
}

impl Packet {
    pub fn new(packet_type: PacketType, sequence: u32, flags: u16, timestamp_us: u64, payload: Vec<u8>) -> Result<Self> {
        if payload.len() > u16::MAX as usize {
            return Err(ProtocolError::Serialization(format!(
                "Payload size {} exceeds u16 limit",
                payload.len()
            )));
        }

        let header = Header::new(
            packet_type,
            sequence,
            flags,
            timestamp_us,
            payload.len() as u16,
        );

        Ok(Self { header, payload })
    }

    pub fn encode(&self) -> Vec<u8> {
        let total_len = HEADER_SIZE + self.payload.len();
        let mut buf = vec![0u8; total_len];
        self.header.encode(&mut buf[0..HEADER_SIZE]).expect("Header size guaranteed valid");
        buf[HEADER_SIZE..].copy_from_slice(&self.payload);
        buf
    }

    pub fn encode_into(&self, dst: &mut [u8]) -> Result<usize> {
        let total_len = HEADER_SIZE + self.payload.len();
        if dst.len() < total_len {
            return Err(ProtocolError::BufferTooShort {
                expected: total_len,
                actual: dst.len(),
            });
        }

        self.header.encode(&mut dst[0..HEADER_SIZE])?;
        dst[HEADER_SIZE..total_len].copy_from_slice(&self.payload);
        Ok(total_len)
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        let header = Header::decode(src)?;
        let expected_total = HEADER_SIZE + header.payload_len as usize;

        if src.len() < expected_total {
            return Err(ProtocolError::PayloadLengthMismatch {
                header_len: header.payload_len as usize,
                actual_len: src.len().saturating_sub(HEADER_SIZE),
            });
        }

        let payload = src[HEADER_SIZE..expected_total].to_vec();
        Ok(Self { header, payload })
    }

    pub fn is_reliable(&self) -> bool {
        (self.header.flags & flags::RELIABLE) != 0
    }

    pub fn is_fragmented(&self) -> bool {
        (self.header.flags & flags::FRAGMENTED) != 0
    }

    pub fn is_keyframe(&self) -> bool {
        (self.header.flags & flags::KEYFRAME) != 0
    }
}
