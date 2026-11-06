use purity_sv2::wire::Connection;
use stratum_core::{
    bitcoin::secp256k1::{Keypair, Secp256k1, SecretKey},
    common_messages_sv2::SetupConnectionSuccess,
    parsers_sv2::{AnyMessage, CommonMessages},
};
use tokio::net::TcpListener;

#[tokio::test(flavor = "current_thread")]
async fn authenticated_noise_roundtrip_and_wrong_authority() {
    let secret = [1u8; 32];
    let pair =
        Keypair::from_secret_key(&Secp256k1::new(), &SecretKey::from_slice(&secret).unwrap());
    let public = pair.x_only_public_key().0.serialize();
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let server = tokio::spawn(async move {
        let (socket, _) = listener.accept().await.unwrap();
        let mut connection = Connection::respond(socket, public, secret).await.unwrap();
        connection
            .send(AnyMessage::Common(CommonMessages::SetupConnectionSuccess(
                SetupConnectionSuccess {
                    used_version: 2,
                    flags: 0,
                },
            )))
            .await
            .unwrap();
        let (socket, _) = listener.accept().await.unwrap();
        let _ = Connection::respond(socket, public, secret).await;
    });
    let mut client = Connection::connect(&address.to_string(), public)
        .await
        .unwrap();
    let frame = client.receive().await.unwrap();
    assert_eq!(frame.kind, 1);
    assert_eq!(frame.payload, [2, 0, 0, 0, 0, 0]);
    let wrong_pair = Keypair::from_secret_key(
        &Secp256k1::new(),
        &SecretKey::from_slice(&[2u8; 32]).unwrap(),
    );
    assert!(Connection::connect(
        &address.to_string(),
        wrong_pair.x_only_public_key().0.serialize()
    )
    .await
    .is_err());
    server.await.unwrap();
}
