// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
use std::{io, time::Duration};
use stratum_core::{
    codec_sv2::{
        Decrypted, Handshake, MessageFrame, NoiseDecoder, NoiseEncoder, Transport,
        TransportDecryptState, TransportEncryptState,
    },
    noise_sv2::{
        Initiator, Responder, ELLSWIFT_ENCODING_SIZE, INITIATOR_EXPECTED_HANDSHAKE_MESSAGE_SIZE,
    },
    parsers_sv2::{AnyMessage, AnyMessageOwned, IsSv2Message},
};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
    time::timeout,
};

pub type Result<T> = std::result::Result<T, io::Error>;
pub fn protocol_error() -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, "SV2 protocol failure")
}
pub struct Frame {
    pub kind: u8,
    pub payload: Vec<u8>,
}
pub struct Connection {
    stream: TcpStream,
    encoder: NoiseEncoder,
    decoder: NoiseDecoder,
    encrypt: TransportEncryptState,
    decrypt: Option<TransportDecryptState>,
}
impl Connection {
    fn new(stream: TcpStream, transport: Transport) -> Self {
        let (encrypt, decrypt) = transport.split();
        Self {
            stream,
            encoder: NoiseEncoder::new(),
            decoder: NoiseDecoder::new(),
            encrypt,
            decrypt: Some(decrypt),
        }
    }
    pub async fn connect(address: &str, authority: [u8; 32]) -> Result<Self> {
        timeout(Duration::from_secs(10), async {
            let mut stream = TcpStream::connect(address).await?;
            stream.set_nodelay(true)?;
            let role = Initiator::from_raw_k(authority).map_err(|_| protocol_error())?;
            let (first, sent) = Handshake::initiator(role)
                .step_0()
                .map_err(|_| protocol_error())?;
            stream.write_all(first.payload()).await?;
            let mut second = [0; INITIATOR_EXPECTED_HANDSHAKE_MESSAGE_SIZE];
            stream.read_exact(&mut second).await?;
            let transport = sent.step_2(second).map_err(|_| protocol_error())?;
            Ok(Self::new(stream, transport))
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "SV2 connection timeout"))?
    }
    pub async fn respond(
        mut stream: TcpStream,
        public: [u8; 32],
        secret: [u8; 32],
    ) -> Result<Self> {
        timeout(Duration::from_secs(10), async {
            let role = Responder::from_authority_kp(&public, &secret, Duration::from_secs(3600))
                .map_err(|_| protocol_error())?;
            let mut first = [0; ELLSWIFT_ENCODING_SIZE];
            stream.read_exact(&mut first).await?;
            let (second, transport) = Handshake::responder(role)
                .step_1(first)
                .map_err(|_| protocol_error())?;
            stream.write_all(second.payload()).await?;
            Ok(Self::new(stream, transport))
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "SV2 handshake timeout"))?
    }
    pub async fn send(&mut self, message: AnyMessage<'_>) -> Result<()> {
        self.send_owned(message.into_owned()).await
    }
    pub async fn send_owned(&mut self, message: AnyMessageOwned) -> Result<()> {
        let frame: MessageFrame<AnyMessageOwned> =
            message.try_into().map_err(|_| protocol_error())?;
        let bytes = self
            .encoder
            .encode_transport(frame, &mut self.encrypt)
            .map_err(|_| protocol_error())?;
        timeout(
            Duration::from_secs(10),
            self.stream.write_all(bytes.as_ref()),
        )
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "SV2 write timeout"))??;
        Ok(())
    }
    pub async fn receive(&mut self) -> Result<Frame> {
        loop {
            let n = self.stream.read(self.decoder.read_buf()).await?;
            if n == 0 {
                return Err(io::ErrorKind::UnexpectedEof.into());
            }
            self.decoder.advance(n).map_err(|_| protocol_error())?;
            let state = self.decrypt.take().ok_or_else(protocol_error)?;
            match self
                .decoder
                .next_transport_frame(state)
                .map_err(|_| protocol_error())?
            {
                Decrypted::Incomplete(state) => self.decrypt = Some(state),
                Decrypted::Frame(mut frame, state) => {
                    self.decrypt = Some(state);
                    let header = frame.header();
                    validate_payload(header, frame.payload())?;
                    return Ok(Frame {
                        kind: header.msg_type(),
                        payload: frame.payload().to_vec(),
                    });
                }
            }
        }
    }
}

fn validate_payload(
    header: stratum_core::framing_sv2::header::Header,
    payload: &mut [u8],
) -> Result<()> {
    if header.ext_type_without_channel_msg() != 0 || header.payload_length() != payload.len() {
        return Err(protocol_error());
    }
    let message = AnyMessage::try_from((header, payload)).map_err(|_| protocol_error())?;
    if header.channel_msg() != message.channel_bit() {
        return Err(protocol_error());
    }
    Ok(())
}
pub fn validate_plain_frame(bytes: &mut [u8]) -> Result<()> {
    if bytes.len() < 6 {
        return Err(protocol_error());
    }
    let (header, payload) = bytes.split_at_mut(6);
    let header = stratum_core::framing_sv2::header::Header::from_bytes(header)
        .map_err(|_| protocol_error())?;
    validate_payload(header, payload)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn malformed_frames_are_bounded_and_do_not_panic() {
        let mut seed = 0x81274abdu64;
        for n in 0..20000usize {
            let size = n % 256;
            let mut bytes = vec![0u8; 6 + size];
            bytes[2] = (n % 256) as u8;
            bytes[3] = size as u8;
            for byte in &mut bytes[6..] {
                seed ^= seed << 13;
                seed ^= seed >> 7;
                seed ^= seed << 17;
                *byte = seed as u8;
            }
            let _ = validate_plain_frame(&mut bytes);
        }
        assert!(validate_plain_frame(&mut [0; 5]).is_err());
        let mut wrong_channel = vec![0, 128, 1, 6, 0, 0, 2, 0, 0, 0, 0, 0];
        assert!(validate_plain_frame(&mut wrong_channel).is_err());
    }
}
