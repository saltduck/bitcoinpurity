use purity_sv2::runtime::{Config, Gateway, Node};
use serde_json::Value;
use std::{io::Read, sync::Arc, time::Duration};
struct NoTemplates;
impl Node for NoTemplates {
    fn request(&self, _: Value) -> Result<Value, ()> {
        panic!("disconnected pool must not request templates")
    }
}
#[test]
fn unavailable_pool_exposes_no_miner_work_and_stop_joins() {
    let cfg: Config = serde_json::from_value(serde_json::json!({
        "listen":"127.0.0.1:0", "pool":"127.0.0.1:1", "jds":"127.0.0.1:1",
        "pool_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "jds_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "identity":"test"
    }))
    .unwrap();
    let gateway = Gateway::start(cfg, Arc::new(NoTemplates)).unwrap();
    let mut socket = std::net::TcpStream::connect(gateway.address).unwrap();
    socket
        .set_read_timeout(Some(Duration::from_secs(2)))
        .unwrap();
    let mut byte = [0; 1];
    assert_eq!(socket.read(&mut byte).unwrap(), 0);
    assert!(!gateway.status().running);
    let start = std::time::Instant::now();
    gateway.stop();
    assert!(start.elapsed() < Duration::from_secs(2));
}

#[test]
fn stop_cancels_an_incomplete_noise_handshake() {
    use std::io::Read;
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let address = listener.local_addr().unwrap();
    let (ready, accepted) = std::sync::mpsc::sync_channel(1);
    let peer = std::thread::spawn(move || {
        let (mut socket, _) = listener.accept().unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(3)))
            .unwrap();
        let mut first = [0u8; 64];
        socket.read_exact(&mut first).unwrap();
        ready.send(()).unwrap();
        assert_eq!(socket.read(&mut [0u8; 1]).unwrap(), 0);
    });
    let cfg: Config = serde_json::from_value(serde_json::json!({
        "listen":"127.0.0.1:0","pool":address.to_string(),"jds":"127.0.0.1:1",
        "pool_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "jds_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "identity":"test"
    }))
    .unwrap();
    let gateway = Gateway::start(cfg, Arc::new(NoTemplates)).unwrap();
    accepted.recv_timeout(Duration::from_secs(2)).unwrap();
    let start = std::time::Instant::now();
    gateway.stop();
    assert!(start.elapsed() < Duration::from_secs(2));
    peer.join().unwrap();
}
