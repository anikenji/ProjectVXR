//! # VXR Protocol
//!
//! High-performance network wire protocol, FEC (Forward Error Correction),
//! and UDP socket management for Project VXR (Wireless VR/Desktop Streaming).

pub mod error;
pub mod fec;
pub mod packet;
pub mod payloads;
pub mod socket;
pub mod telemetry;

pub use error::{ProtocolError, Result};
pub use fec::{FecDecoder, FecEncoder, FecHeader, FecShard};
pub use packet::{flags, Header, Packet, PacketType, HEADER_SIZE, PROTOCOL_MAGIC, PROTOCOL_VERSION};
pub use payloads::{
    AudioPayload, ControlPayload, ControllerState, FeedbackPayload, HardwareCursor, InputPayload,
    Pose6DoF, VideoCodec, VideoFragmenter, VideoPayload, VideoReassembler,
};
pub use socket::{SocketConfig, SocketStats, VxrSocket, DSCP_EXPEDITED_FORWARDING};
pub use telemetry::{
    VxrFrameData, VxrFrameMetadata, VxrHandTracking26, VxrJointPose, VxrTouchControllerTelemetry,
    VXR_FRAME_DATA_WIRE_SIZE,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_packet_header_encode_decode() {
        let header = Header::new(PacketType::Video, 42, flags::KEYFRAME | flags::RELIABLE, 1234567890, 512);
        let mut buf = [0u8; HEADER_SIZE];
        header.encode(&mut buf).expect("Encode must succeed");

        let decoded = Header::decode(&buf).expect("Decode must succeed");
        assert_eq!(decoded.magic, PROTOCOL_MAGIC);
        assert_eq!(decoded.version, PROTOCOL_VERSION);
        assert_eq!(decoded.packet_type, PacketType::Video);
        assert_eq!(decoded.sequence, 42);
        assert_eq!(decoded.flags, flags::KEYFRAME | flags::RELIABLE);
        assert_eq!(decoded.timestamp_us, 1234567890);
        assert_eq!(decoded.payload_len, 512);
    }

    #[test]
    fn test_packet_full_roundtrip() {
        let payload = vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
        let packet = Packet::new(PacketType::Audio, 101, flags::NONE, 999999, payload.clone()).unwrap();
        let encoded = packet.encode();

        let decoded = Packet::decode(&encoded).expect("Decode packet must succeed");
        assert_eq!(decoded.header.packet_type, PacketType::Audio);
        assert_eq!(decoded.header.sequence, 101);
        assert_eq!(decoded.header.timestamp_us, 999999);
        assert_eq!(decoded.payload, payload);
    }

    #[test]
    fn test_video_payload_and_fragmentation() {
        let fake_frame = vec![0xAB; 5000];
        let fragments = VideoFragmenter::fragment_frame(
            1,
            VideoCodec::Hevc,
            true,
            false,
            false,
            1_000_000,
            &fake_frame,
            1200,
        );

        assert_eq!(fragments.len(), 5); // 5000 / 1200 = 4 full + 1 remainder
        for (i, frag) in fragments.iter().enumerate() {
            assert_eq!(frag.frame_index, 1);
            assert_eq!(frag.fragment_index, i as u16);
            assert_eq!(frag.total_fragments, 5);
            assert_eq!(frag.codec, VideoCodec::Hevc);
            assert!(frag.is_keyframe);
        }

        // Test reassembly
        let mut reassembler = VideoReassembler::new(10);
        let mut assembled_frame = None;

        // Feed fragments (simulate out-of-order arrival: 1, 0, 3, 2, 4)
        let order = [1, 0, 3, 2, 4];
        for &idx in &order {
            let res = reassembler.insert(fragments[idx].clone());
            if res.is_some() {
                assembled_frame = res;
            }
        }

        assert!(assembled_frame.is_some());
        let (f_idx, codec, keyframe, pts, data) = assembled_frame.unwrap();
        assert_eq!(f_idx, 1);
        assert_eq!(codec, VideoCodec::Hevc);
        assert!(keyframe);
        assert_eq!(pts, 1_000_000);
        assert_eq!(data, fake_frame);
    }

    #[test]
    fn test_fec_encode_and_reconstruction() {
        // Setup 8 data shards + 2 parity shards (20% redundancy)
        let data_shards = 8;
        let parity_shards = 2;
        let encoder = FecEncoder::new(data_shards, parity_shards).unwrap();

        // 8 distinct test payloads
        let mut test_payloads = Vec::new();
        for i in 0..data_shards {
            test_payloads.push(vec![(i + 1) as u8; 100 + i * 10]);
        }
        let slices: Vec<&[u8]> = test_payloads.iter().map(|p| p.as_slice()).collect();

        // Generate parity shards
        let group_id = 100;
        let parity_shards_list = encoder.encode(group_id, &slices).unwrap();
        assert_eq!(parity_shards_list.len(), parity_shards);

        // Test recovery when data shards 2 and 5 are lost in transmission!
        let mut decoder = FecDecoder::new(10);

        // Receive data shards 0, 1, 3, 4, 6, 7 (shards 2 and 5 lost!)
        for i in [0, 1, 3, 4, 6, 7] {
            let res = decoder.add_data_shard(group_id, i, data_shards, parity_shards, &test_payloads[i]).unwrap();
            assert!(res.is_none()); // Not enough yet
        }

        // Receive parity shard 0 (total received = 6 data + 1 parity = 7, still need 1 more)
        let res1 = decoder.add_parity_shard(&parity_shards_list[0]).unwrap();
        assert!(res1.is_none());

        // Receive parity shard 1 (total received = 6 data + 2 parity = 8 == data_shards -> recovery triggers!)
        let recovered = decoder.add_parity_shard(&parity_shards_list[1]).unwrap();
        assert!(recovered.is_some(), "Recovery must succeed with any 8 of 10 shards");

        let recovered_data = recovered.unwrap();
        assert_eq!(recovered_data.len(), data_shards);
        for i in 0..data_shards {
            assert_eq!(recovered_data[i], test_payloads[i], "Shard {} correctly reconstructed", i);
        }
    }

    #[test]
    fn test_udp_socket_loopback() {
        let config = SocketConfig {
            non_blocking: false,
            ..Default::default()
        };

        let server = VxrSocket::bind("127.0.0.1:0".parse().unwrap(), config.clone()).unwrap();
        let server_addr = server.local_addr().unwrap();

        let client = VxrSocket::bind("127.0.0.1:0".parse().unwrap(), config).unwrap();

        let test_payload = vec![11, 22, 33, 44, 55];
        let test_packet = Packet::new(PacketType::Control, 1, flags::NONE, 55555, test_payload.clone()).unwrap();

        // Send packet client -> server
        client.send_packet(&test_packet, &server_addr).unwrap();

        let mut recv_buf = [0u8; 2048];
        let (recv_packet, sender_addr) = server.recv_packet(&mut recv_buf).unwrap();

        assert_eq!(recv_packet.header.packet_type, PacketType::Control);
        assert_eq!(recv_packet.header.sequence, 1);
        assert_eq!(recv_packet.payload, test_payload);
        assert_eq!(sender_addr, client.local_addr().unwrap());

        assert_eq!(client.stats().packets_sent.load(std::sync::atomic::Ordering::Relaxed), 1);
        assert_eq!(server.stats().packets_recv.load(std::sync::atomic::Ordering::Relaxed), 1);
    }

    #[test]
    fn test_control_payload_roundtrip() {
        let req = ControlPayload::HandshakeRequest {
            client_version: 1,
            headset_model: "Meta Quest 3".to_string(),
            preferred_codec: VideoCodec::Av1,
            display_width: 2064,
            display_height: 2208,
            refresh_rate: 90,
        };

        let encoded = req.encode().unwrap();
        let decoded = ControlPayload::decode(&encoded).unwrap();
        assert_eq!(req, decoded);
    }

    #[test]
    fn test_input_payload_roundtrip() {
        let input = InputPayload {
            timestamp_us: 123456,
            head_pose: Pose6DoF {
                position: [0.1, 1.7, -0.5],
                orientation: [0.0, 0.0, 0.0, 1.0],
                linear_velocity: [0.0; 3],
                angular_velocity: [0.0; 3],
            },
            left_controller: ControllerState {
                is_active: true,
                trigger_value: 0.85,
                ..Default::default()
            },
            right_controller: ControllerState::default(),
            cursor: Some(HardwareCursor {
                x: 0.5,
                y: 0.5,
                visible: true,
            }),
        };

        let encoded = input.encode().unwrap();
        let decoded = InputPayload::decode(&encoded).unwrap();
        assert_eq!(input, decoded);
    }
}
