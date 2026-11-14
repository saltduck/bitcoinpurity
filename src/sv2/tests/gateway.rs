use purity_sv2::runtime::{Config, Gateway, Node};
use serde_json::Value;
use std::{io::{Read,Write}, sync::Arc, time::Duration};
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
    socket.write_all(b"{\"id\":1,\"method\":\"mining.configure\",\"params\":[[\"version-rolling\"],{}]}\n{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"miner\",\"x\"]}\n{\"id\":3,\"method\":\"mining.subscribe\",\"params\":[]}\n").unwrap();
    let mut replies=String::new();
    socket.read_to_string(&mut replies).unwrap();
    let replies:Vec<Value>=replies.lines().map(|line|serde_json::from_str(line).unwrap()).collect();
    assert_eq!(replies.len(),3);
    assert_eq!(replies[0]["result"]["version-rolling"],true);
    assert_eq!(replies[1]["result"],true);
    assert_eq!(replies[2]["id"],3);
    assert!(replies[2]["result"].is_null());
    assert_eq!(replies[2]["error"][0],20);
    assert_eq!(gateway.status().rejected_shares,0);
    let deadline=std::time::Instant::now()+Duration::from_secs(2);
    while gateway.status().clients!=0 && std::time::Instant::now()<deadline {std::thread::sleep(Duration::from_millis(1));}
    assert_eq!(gateway.status().clients,0);
    assert_eq!(gateway.status().authorized_clients,0);
    assert_eq!(gateway.status().subscribed_clients,0);
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

#[test]
fn unavailable_clients_still_obey_connection_limits() {
    let cfg:Config=serde_json::from_value(serde_json::json!({
        "listen":"127.0.0.1:0","pool":"127.0.0.1:1","jds":"127.0.0.1:1",
        "pool_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "jds_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "identity":"test","max_clients":1,"max_per_ip":1
    })).unwrap();
    let gateway=Gateway::start(cfg,Arc::new(NoTemplates)).unwrap();
    let first=std::net::TcpStream::connect(gateway.address).unwrap();
    let deadline=std::time::Instant::now()+Duration::from_secs(2);
    while gateway.status().clients!=1 && std::time::Instant::now()<deadline {std::thread::sleep(Duration::from_millis(1));}
    assert_eq!(gateway.status().clients,1);
    let mut second=std::net::TcpStream::connect(gateway.address).unwrap();
    second.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
    assert_eq!(second.read(&mut [0;1]).unwrap(),0);
    drop(first);
    gateway.stop();
}
