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
    parsers_sv2::{AnyMessage, AnyMessageOwned},
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
                    if header.ext_type() & 0x7fff != 0 {
                        return Err(protocol_error());
                    }
                    return Ok(Frame {
                        kind: header.msg_type(),
                        payload: frame.payload().to_vec(),
                    });
                }
            }
        }
    }
}
