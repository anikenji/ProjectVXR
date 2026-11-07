use byteorder::{ByteOrder, LittleEndian};
use reed_solomon_erasure::galois_8::ReedSolomon;
use std::collections::HashMap;
use crate::error::{ProtocolError, Result};
use crate::packet::{flags, Packet, PacketType};

/// FEC Header: 12 bytes
/// [0..4]: group_id (u32)
/// [4..6]: shard_index (u16)
/// [6..8]: data_shards (u16)
/// [8..10]: parity_shards (u16)
/// [10..12]: shard_len (u16) - total uniform length of each shard in this group
pub const FEC_HEADER_SIZE: usize = 12;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FecHeader {
    pub group_id: u32,
    pub shard_index: u16,
    pub data_shards: u16,
    pub parity_shards: u16,
    pub shard_len: u16,
}

impl FecHeader {
    pub fn encode(&self, dst: &mut [u8]) -> Result<()> {
        if dst.len() < FEC_HEADER_SIZE {
            return Err(ProtocolError::BufferTooShort {
                expected: FEC_HEADER_SIZE,
                actual: dst.len(),
            });
        }
        LittleEndian::write_u32(&mut dst[0..4], self.group_id);
        LittleEndian::write_u16(&mut dst[4..6], self.shard_index);
        LittleEndian::write_u16(&mut dst[6..8], self.data_shards);
        LittleEndian::write_u16(&mut dst[8..10], self.parity_shards);
        LittleEndian::write_u16(&mut dst[10..12], self.shard_len);
        Ok(())
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        if src.len() < FEC_HEADER_SIZE {
            return Err(ProtocolError::BufferTooShort {
                expected: FEC_HEADER_SIZE,
                actual: src.len(),
            });
        }
        let group_id = LittleEndian::read_u32(&src[0..4]);
        let shard_index = LittleEndian::read_u16(&src[4..6]);
        let data_shards = LittleEndian::read_u16(&src[6..8]);
        let parity_shards = LittleEndian::read_u16(&src[8..10]);
        let shard_len = LittleEndian::read_u16(&src[10..12]);

        Ok(Self {
            group_id,
            shard_index,
            data_shards,
            parity_shards,
            shard_len,
        })
    }
}

/// Shard containing header and raw shard data
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FecShard {
    pub header: FecHeader,
    pub data: Vec<u8>,
}

impl FecShard {
    pub fn encode(&self) -> Vec<u8> {
        let mut buf = vec![0u8; FEC_HEADER_SIZE + self.data.len()];
        self.header.encode(&mut buf[0..FEC_HEADER_SIZE]).expect("Header valid");
        buf[FEC_HEADER_SIZE..].copy_from_slice(&self.data);
        buf
    }

    pub fn decode(src: &[u8]) -> Result<Self> {
        let header = FecHeader::decode(src)?;
        let data = src[FEC_HEADER_SIZE..].to_vec();
        Ok(Self { header, data })
    }
}

/// FEC Encoder creates parity shards for a group of data payloads
pub struct FecEncoder {
    data_shards: usize,
    parity_shards: usize,
    codec: ReedSolomon,
}

impl FecEncoder {
    pub fn new(data_shards: usize, parity_shards: usize) -> Result<Self> {
        if data_shards == 0 || parity_shards == 0 || data_shards + parity_shards > 256 {
            return Err(ProtocolError::InvalidFecConfig {
                data: data_shards,
                parity: parity_shards,
            });
        }

        let codec = ReedSolomon::new(data_shards, parity_shards)
            .map_err(|e| ProtocolError::Fec(e.to_string()))?;

        Ok(Self {
            data_shards,
            parity_shards,
            codec,
        })
    }

    /// Formats data payload into an internal shard with a 2-byte length prefix
    pub fn format_data_shard(payload: &[u8], target_len: usize) -> Vec<u8> {
        let mut shard = vec![0u8; target_len];
        LittleEndian::write_u16(&mut shard[0..2], payload.len() as u16);
        shard[2..2 + payload.len()].copy_from_slice(payload);
        shard
    }

    /// Extracts original payload from an internal shard using the 2-byte length prefix
    pub fn extract_data_payload(shard: &[u8]) -> Result<Vec<u8>> {
        if shard.len() < 2 {
            return Err(ProtocolError::BufferTooShort {
                expected: 2,
                actual: shard.len(),
            });
        }
        let orig_len = LittleEndian::read_u16(&shard[0..2]) as usize;
        let end = 2 + orig_len;
        if shard.len() < end {
            return Err(ProtocolError::BufferTooShort {
                expected: end,
                actual: shard.len(),
            });
        }
        Ok(shard[2..end].to_vec())
    }

    /// Encodes a slice of data payloads into parity shards.
    /// Each shard has an embedded 2-byte length prefix, enabling self-contained length recovery.
    pub fn encode(&self, group_id: u32, data_payloads: &[&[u8]]) -> Result<Vec<FecShard>> {
        if data_payloads.len() != self.data_shards {
            return Err(ProtocolError::Fec(format!(
                "Expected {} data payloads, got {}",
                self.data_shards,
                data_payloads.len()
            )));
        }

        let max_payload_len = data_payloads.iter().map(|p| p.len()).max().unwrap_or(0);
        let shard_len = 2 + max_payload_len; // 2-byte length prefix + max payload

        // Prepare shards with uniform length
        let mut shards: Vec<Vec<u8>> = Vec::with_capacity(self.data_shards + self.parity_shards);
        for p in data_payloads {
            shards.push(Self::format_data_shard(p, shard_len));
        }

        for _ in 0..self.parity_shards {
            shards.push(vec![0u8; shard_len]);
        }

        self.codec
            .encode(&mut shards)
            .map_err(|e| ProtocolError::Fec(e.to_string()))?;

        // Return parity shards (indices from data_shards to data_shards + parity_shards - 1)
        let mut parity_shards = Vec::with_capacity(self.parity_shards);
        for (i, shard_bytes) in shards.into_iter().skip(self.data_shards).enumerate() {
            let shard_index = (self.data_shards + i) as u16;
            let header = FecHeader {
                group_id,
                shard_index,
                data_shards: self.data_shards as u16,
                parity_shards: self.parity_shards as u16,
                shard_len: shard_len as u16,
            };
            parity_shards.push(FecShard {
                header,
                data: shard_bytes,
            });
        }

        Ok(parity_shards)
    }

    /// Helper to convert parity shards into standard VXR Packets
    pub fn create_parity_packets(
        &self,
        group_id: u32,
        data_payloads: &[&[u8]],
        base_seq: u32,
        timestamp_us: u64,
    ) -> Result<Vec<Packet>> {
        let shards = self.encode(group_id, data_payloads)?;
        let mut packets = Vec::with_capacity(shards.len());

        for (i, shard) in shards.into_iter().enumerate() {
            let payload = shard.encode();
            let packet = Packet::new(
                PacketType::Fec,
                base_seq + i as u32,
                flags::FEC_PROTECTED,
                timestamp_us,
                payload,
            )?;
            packets.push(packet);
        }

        Ok(packets)
    }
}

/// Tracks group collection and performs recovery
struct GroupState {
    data_shards: usize,
    parity_shards: usize,
    shard_len: usize,
    raw_payloads: Vec<Option<Vec<u8>>>,
    shards: Vec<Option<Vec<u8>>>,
    received_count: usize,
    data_received_count: usize,
}

/// FEC Decoder collects data and parity shards, reconstructing lost packets
pub struct FecDecoder {
    groups: HashMap<u32, GroupState>,
    max_active_groups: usize,
}

impl FecDecoder {
    pub fn new(max_active_groups: usize) -> Self {
        Self {
            groups: HashMap::new(),
            max_active_groups,
        }
    }

    /// Receives a data payload (index 0..data_shards - 1)
    pub fn add_data_shard(
        &mut self,
        group_id: u32,
        shard_index: usize,
        data_shards: usize,
        parity_shards: usize,
        data: &[u8],
    ) -> Result<Option<Vec<Vec<u8>>>> {
        let total = data_shards + parity_shards;
        let formatted = FecEncoder::format_data_shard(data, 2 + data.len());

        let state = self.groups.entry(group_id).or_insert_with(|| GroupState {
            data_shards,
            parity_shards,
            shard_len: 0,
            raw_payloads: vec![None; data_shards],
            shards: vec![None; total],
            received_count: 0,
            data_received_count: 0,
        });

        if shard_index < state.shards.len() && state.shards[shard_index].is_none() {
            state.raw_payloads[shard_index] = Some(data.to_vec());
            state.shards[shard_index] = Some(formatted);
            state.received_count += 1;
            state.data_received_count += 1;
            if 2 + data.len() > state.shard_len {
                state.shard_len = 2 + data.len();
            }
        }

        self.try_recover(group_id)
    }

    /// Receives a parity shard (index data_shards..total - 1)
    pub fn add_parity_shard(&mut self, shard: &FecShard) -> Result<Option<Vec<Vec<u8>>>> {
        let group_id = shard.header.group_id;
        let shard_index = shard.header.shard_index as usize;
        let data_shards = shard.header.data_shards as usize;
        let parity_shards = shard.header.parity_shards as usize;
        let shard_len = shard.header.shard_len as usize;
        let total = data_shards + parity_shards;

        let state = self.groups.entry(group_id).or_insert_with(|| GroupState {
            data_shards,
            parity_shards,
            shard_len,
            raw_payloads: vec![None; data_shards],
            shards: vec![None; total],
            received_count: 0,
            data_received_count: 0,
        });

        if shard_index < state.shards.len() && state.shards[shard_index].is_none() {
            if shard_len > state.shard_len {
                state.shard_len = shard_len;
            }
            state.shards[shard_index] = Some(shard.data.clone());
            state.received_count += 1;
        }

        self.try_recover(group_id)
    }

    fn try_recover(&mut self, group_id: u32) -> Result<Option<Vec<Vec<u8>>>> {
        let state = match self.groups.get_mut(&group_id) {
            Some(s) => s,
            None => return Ok(None),
        };

        // If all data shards are already present, no reconstruction needed
        if state.data_received_count == state.data_shards {
            let mut result = Vec::with_capacity(state.data_shards);
            for i in 0..state.data_shards {
                if let Some(ref raw) = state.raw_payloads[i] {
                    result.push(raw.clone());
                }
            }
            self.groups.remove(&group_id);
            self.prune();
            return Ok(Some(result));
        }

        // If we have received >= data_shards total (data + parity), perform Reed-Solomon recovery
        if state.received_count >= state.data_shards {
            let shard_len = state.shard_len;
            if shard_len == 0 {
                return Ok(None);
            }

            let codec = ReedSolomon::new(state.data_shards, state.parity_shards)
                .map_err(|e| ProtocolError::Fec(e.to_string()))?;

            // Uniformly pad all shards to shard_len
            let mut padded_shards: Vec<Option<Vec<u8>>> = Vec::with_capacity(state.shards.len());
            for shard_opt in &state.shards {
                if let Some(ref s) = shard_opt {
                    let mut padded = vec![0u8; shard_len];
                    padded[..s.len().min(shard_len)].copy_from_slice(&s[..s.len().min(shard_len)]);
                    padded_shards.push(Some(padded));
                } else {
                    padded_shards.push(None);
                }
            }

            codec
                .reconstruct_data(&mut padded_shards)
                .map_err(|e| ProtocolError::Fec(e.to_string()))?;

            let mut recovered = Vec::with_capacity(state.data_shards);
            for (i, shard) in padded_shards.iter().take(state.data_shards).enumerate() {
                if let Some(ref raw) = state.raw_payloads[i] {
                    recovered.push(raw.clone());
                } else if let Some(ref shard_bytes) = shard {
                    let extracted = FecEncoder::extract_data_payload(shard_bytes)?;
                    recovered.push(extracted);
                } else {
                    return Err(ProtocolError::Fec(format!(
                        "Failed to recover shard {} for group {}",
                        i, group_id
                    )));
                }
            }

            self.groups.remove(&group_id);
            self.prune();
            return Ok(Some(recovered));
        }

        Ok(None)
    }

    fn prune(&mut self) {
        if self.groups.len() > self.max_active_groups {
            if let Some(min_id) = self.groups.keys().copied().min() {
                self.groups.remove(&min_id);
            }
        }
    }
}
