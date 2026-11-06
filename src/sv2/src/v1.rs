// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
use crate::{
    model::Job,
    runtime::{stopped, Config, Share, Shared},
    wire::{protocol_error, Result},
};
use futures_util::{SinkExt, StreamExt};
use serde_json::{json, Value};
use std::{
    collections::{HashMap, VecDeque},
    net::{IpAddr, SocketAddr},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    time::{Duration, Instant},
};
use stratum_core::{
    bitcoin::Target, stratum_translation::sv2_to_sv1::build_sv1_set_difficulty_from_sv2_target,
};
use tokio::{
    net::{TcpListener, TcpStream},
    sync::{mpsc, oneshot, watch},
    task::JoinSet,
    time::{sleep, sleep_until, timeout},
};
use tokio_util::codec::{Framed, LinesCodec};
struct ClientGuard {
    shared: Arc<Shared>,
    counts: Arc<Mutex<HashMap<IpAddr, usize>>>,
    ip: IpAddr,
    authorized: Arc<AtomicBool>,
    subscribed: Arc<AtomicBool>,
}
impl Drop for ClientGuard {
    fn drop(&mut self) {
        let mut counts = self.counts.lock().unwrap_or_else(|p| p.into_inner());
        if let Some(n) = counts.get_mut(&self.ip) {
            *n -= 1;
            if *n == 0 {
                counts.remove(&self.ip);
            }
        }
        self.shared.update(|s| {
            s.clients -= 1;
            if self.subscribed.load(Ordering::Relaxed) {
                s.subscribed_clients -= 1;
            }
            if self.authorized.load(Ordering::Relaxed) {
                s.authorized_clients -= 1;
            }
        });
    }
}
async fn work(job: &Job, out: &mpsc::Sender<Value>) -> Result<()> {
    let difficulty = build_sv1_set_difficulty_from_sv2_target(Target::from_le_bytes(job.target))
        .map_err(|_| protocol_error())?;
    out.send(serde_json::to_value(difficulty).map_err(|_| protocol_error())?)
        .await
        .map_err(|_| protocol_error())?;
    out.send(job.notify.clone())
        .await
        .map_err(|_| protocol_error())?;
    Ok(())
}
// Accept errors are per connection (ECONNABORTED, pending network errors) or
// transient resource limits (EMFILE/ENFILE); the pause bounds retry spinning.
const ACCEPT_RETRY: Duration = Duration::from_millis(100);
pub(crate) trait Acceptor {
    async fn accept(&mut self) -> std::io::Result<(TcpStream, SocketAddr)>;
}
impl Acceptor for TcpListener {
    async fn accept(&mut self) -> std::io::Result<(TcpStream, SocketAddr)> {
        TcpListener::accept(self).await
    }
}
pub(crate) async fn serve(
    mut listener: impl Acceptor,
    cfg: Config,
    jobs: watch::Receiver<Option<Arc<Job>>>,
    shares: mpsc::Sender<Share>,
    shared: Arc<Shared>,
) {
    let counts = Arc::new(Mutex::new(HashMap::new()));
    let mut tasks = JoinSet::new();
    let mut serial = 0u64;
    loop {
        tokio::select! {
            _=stopped(&shared)=>break,
            _=tasks.join_next(),if !tasks.is_empty()=>{}
            accepted=listener.accept()=> {
                let (socket,address)=match accepted {
                    Ok(accepted)=>accepted,
                    Err(_)=>{
                        tokio::select! {_=stopped(&shared)=>break,_=sleep(ACCEPT_RETRY)=>{}}
                        continue;
                    }
                };
                let count:usize=counts.lock().unwrap_or_else(|p|p.into_inner()).values().sum();
                let per_ip=*counts.lock().unwrap_or_else(|p|p.into_inner()).get(&address.ip()).unwrap_or(&0);
                if count>=cfg.max_clients || per_ip>=cfg.max_per_ip || serial>=u32::MAX as u64 {drop(socket);continue;}
                serial+=1;
                *counts.lock().unwrap_or_else(|p|p.into_inner()).entry(address.ip()).or_insert(0)+=1;
                shared.update(|s|s.clients+=1);
                let authorized=Arc::new(AtomicBool::new(false));
                let subscribed=Arc::new(AtomicBool::new(false));
                let guard=ClientGuard{shared:shared.clone(),counts:counts.clone(),ip:address.ip(),authorized:authorized.clone(),subscribed:subscribed.clone()};
                let cfg=cfg.clone();let jobs=jobs.clone();let shares=shares.clone();let state=shared.clone();
                tasks.spawn(async move {
                    let _guard=guard;
                    let _=client(socket,serial as u32,cfg,jobs,shares,state,authorized,subscribed).await;
                });
            }
        }
    }
    tasks.abort_all();
    while tasks.join_next().await.is_some() {}
}
async fn client(
    socket: TcpStream,
    serial: u32,
    cfg: Config,
    mut jobs: watch::Receiver<Option<Arc<Job>>>,
    shares: mpsc::Sender<Share>,
    shared: Arc<Shared>,
    authorized: Arc<AtomicBool>,
    subscribed_state: Arc<AtomicBool>,
) -> Result<()> {
    socket.set_nodelay(true)?;
    let (mut writer, mut reader) =
        Framed::new(socket, LinesCodec::new_with_max_length(16384)).split();
    let (out, mut outgoing) = mpsc::channel::<Value>(64);
    let (close, mut closing) = oneshot::channel::<()>();
    let writes = async {
        loop {
            let value = tokio::select! {
                value = outgoing.recv() => value,
                _ = &mut closing => {
                    while let Ok(value) = outgoing.try_recv() {
                        timeout(Duration::from_secs(5), writer.send(value.to_string())).await.map_err(|_|protocol_error())?.map_err(|_|protocol_error())?;
                    }
                    return Ok::<_, std::io::Error>(());
                }
            };
            let Some(value) = value else { return Ok::<_, std::io::Error>(()); };
            timeout(Duration::from_secs(5), writer.send(value.to_string()))
                .await
                .map_err(|_| protocol_error())?
                .map_err(|_| protocol_error())?;
        }
    };
    let reads = async {
        let mut subscribed = false;
        let mut mask = 0u32;
        let mut known: VecDeque<Arc<Job>> = VecDeque::new();
        let deadline = tokio::time::Instant::now() + Duration::from_secs(10);
        let mut window = Instant::now();
        let mut requests = 0;
        loop {
            tokio::select! {
                _=sleep_until(deadline),if !subscribed || !authorized.load(Ordering::Relaxed)=>return Err(protocol_error()),
                changed=jobs.changed()=> {
                    changed.map_err(|_|protocol_error())?;
                    let Some(job)=jobs.borrow().clone() else {
                        known.clear();
                        if subscribed {return Err(protocol_error());}
                        continue;
                    };
                    if subscribed && authorized.load(Ordering::Relaxed) {
                        work(&job,&out).await?;known.push_back(job);
                        while known.len()>16 {known.pop_front();}
                    }
                }
                line=reader.next()=> {
                    let line=line.ok_or_else(protocol_error)?.map_err(|_|protocol_error())?;
                    if window.elapsed()>Duration::from_secs(1) {requests=0;window=Instant::now();}
                    requests+=1;if requests>64 {return Err(protocol_error());}
                    let request:Value=serde_json::from_str(&line).map_err(|_|protocol_error())?;
                    let id=request["id"].clone();
                    let params=&request["params"];
                    let mut response=json!({"id":id,"result":true,"error":null});
                    let job=jobs.borrow().clone();
                    match request["method"].as_str().ok_or_else(protocol_error)? {
                        "mining.subscribe"=> {
                            if subscribed {return Err(protocol_error());}
                            let Some(job)=job.as_ref() else {
                                response["result"]=Value::Null;
                                response["error"]=json!([20,"Upstream work unavailable",null]);
                                out.send(response).await.map_err(|_|protocol_error())?;
                                let _ = close.send(());
                                return std::future::pending::<Result<()>>().await;
                            };
                            let mut prefix=job.extranonce_prefix.clone();prefix.extend_from_slice(&serial.to_le_bytes());
                            response["result"]=json!([[["mining.set_difficulty",format!("{serial:x}")],["mining.notify",format!("{serial:x}")]],hex::encode(prefix),job.rollable_size-4]);
                            subscribed=true;
                            subscribed_state.store(true, Ordering::Relaxed);
                            shared.update(|s|s.subscribed_clients+=1);
                        }
                        "mining.authorize"=> {
                            let name=params[0].as_str().ok_or_else(protocol_error)?;
                            let password=params[1].as_str().ok_or_else(protocol_error)?;
                            let valid_name=name==cfg.user || name.strip_prefix(&(cfg.user.clone()+".")).is_some_and(|s|!s.is_empty());
                            let allowed=!cfg.auth || (valid_name && password==cfg.password);
                            response["result"]=json!(allowed);
                            if !allowed {
                                out.send(response).await.map_err(|_|protocol_error())?;
                                continue;
                            }
                            if !authorized.swap(true,Ordering::Relaxed) {shared.update(|s|s.authorized_clients+=1);}
                        }
                        "mining.configure"=> {
                            let requested=params[0].as_array().is_some_and(|a|a.iter().any(|x|x=="version-rolling"));
                            let offered=params[1]["version-rolling.mask"].as_str().unwrap_or("1fffe000");
                            let proposed=u32::from_str_radix(offered,16).map_err(|_|protocol_error())? & 0x1fffe000;
                            let minimum=params[1]["version-rolling.min-bit-count"].as_u64().unwrap_or(0);
                            let allowed=requested && proposed.count_ones() as u64>=minimum;
                            mask=if allowed {proposed}else{0};
                            response["result"]=json!({"version-rolling":allowed,"version-rolling.mask":format!("{mask:08x}")});
                        }
                        "mining.extranonce.subscribe"=>{}
                        "mining.submit"=> {
                            if !subscribed || !authorized.load(Ordering::Relaxed) {return Err(protocol_error());}
                            let job_id=params[1].as_str().ok_or_else(protocol_error)?;
                            let extra=hex::decode(params[2].as_str().ok_or_else(protocol_error)?).map_err(|_|protocol_error())?;
                            let parse_hex=|v:&Value|->Result<u32> {
                                let s=v.as_str().ok_or_else(protocol_error)?;
                                if s.len()!=8 {return Err(protocol_error());}
                                u32::from_str_radix(s,16).map_err(|_|protocol_error())
                            };
                            let ntime=parse_hex(&params[3])?;let nonce=parse_hex(&params[4])?;
                            if !params[5].is_null() { parse_hex(&params[5])?; }
                            let Some(job)=known.iter().rev().find(|j|j.id()==job_id) else {
                                shared.update(|s|s.rejected_shares+=1);
                                response["result"]=json!(false);response["error"]=json!([21,"Unknown work",null]);
                                out.send(response).await.map_err(|_|protocol_error())?;continue;
                            };
                            if extra.len()!=job.rollable_size-4 {return Err(protocol_error());}
                            let version=if params[5].is_null() {job.template.version}else{
                                let bits=parse_hex(&params[5])?;
                                if mask==0 || bits & !mask!=0 {return Err(protocol_error());}
                                (job.template.version & !mask) | bits
                            };
                            let mut extranonce=serial.to_le_bytes().to_vec();extranonce.extend(extra);
                            let share=Share{id,reply:out.clone(),job:job_id.to_string(),extranonce,version,ntime,nonce};
                            if let Err(error)=shares.try_send(share) {shared.reject(error.into_inner());}
                            continue;
                        }
                        _=>{response["result"]=Value::Null;response["error"]=json!([20,"Unsupported method",null]);}
                    }
                    out.send(response).await.map_err(|_|protocol_error())?;
                    if subscribed && authorized.load(Ordering::Relaxed) &&
                        matches!(request["method"].as_str(),Some("mining.subscribe"|"mining.authorize")) {
                        if let Some(job)=job {work(&job,&out).await?;known.push_back(job);}
                        while known.len()>16 {known.pop_front();}
                    }
                }
            }
        }
    };
    tokio::select! {_=stopped(&shared)=>Ok(()),result=reads=>result,result=writes=>result}
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::Status;
    use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
    struct Flaky {
        listener: TcpListener,
        failures: usize,
    }
    impl Acceptor for Flaky {
        async fn accept(&mut self) -> std::io::Result<(TcpStream, SocketAddr)> {
            if self.failures > 0 {
                self.failures -= 1;
                return Err(std::io::ErrorKind::ConnectionAborted.into());
            }
            self.listener.accept().await
        }
    }
    #[tokio::test]
    async fn accept_errors_do_not_stop_the_listener() {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let cfg: Config = serde_json::from_value(json!({"listen":"127.0.0.1:0","pool":"127.0.0.1:1","jds":"127.0.0.1:1",
            "pool_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
            "jds_key":"79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798","identity":"test"}))
        .unwrap();
        let (_jobs, job_receiver) = watch::channel(None);
        let (shares, _receiver) = mpsc::channel(1);
        let shared = Arc::new(Shared::new(Status::default()));
        let server = tokio::spawn(serve(
            Flaky { listener, failures: 3 },
            cfg,
            job_receiver,
            shares,
            shared.clone(),
        ));
        let mut socket = TcpStream::connect(address).await.unwrap();
        socket
            .write_all(b"{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n")
            .await
            .unwrap();
        let mut line = String::new();
        timeout(Duration::from_secs(5), BufReader::new(&mut socket).read_line(&mut line))
            .await
            .expect("listener must keep accepting after accept errors")
            .unwrap();
        assert!(line.contains("Upstream work unavailable"), "{line}");
        shared.stop.store(true, Ordering::Release);
        timeout(Duration::from_secs(5), server).await.unwrap().unwrap();
    }
}
