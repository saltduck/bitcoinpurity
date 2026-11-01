// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
use crate::{
    model::{self, Job, Status, Template},
    wire::{protocol_error, Connection, Result},
};
use serde::Deserialize;
use serde_json::{json, Value};
use std::{
    collections::{BTreeMap, BTreeSet, VecDeque},
    net::SocketAddr,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use stratum_core::{
    binary_sv2,
    bitcoin::{hashes::Hash, Target},
    channels_sv2::{
        chain_tip::ChainTip,
        client::{
            extended::ExtendedChannel,
            share_accounting::{ShareValidationError, ShareValidationResult},
        },
        extranonce_manager::ExtranoncePrefix,
    },
    common_messages_sv2::*,
    job_declaration_sv2::*,
    mining_sv2::*,
    parsers_sv2::{
        AnyMessageOwned as Any, CommonMessagesOwned as Common, JobDeclarationOwned as Jd,
        MiningOwned as Mining,
    },
};
use tokio::{
    net::TcpListener,
    sync::{mpsc, watch},
    time::{interval, sleep, timeout},
};
pub trait Node: Send + Sync {
    fn request(&self, request: Value) -> std::result::Result<Value, ()>;
}
fn default_clients() -> usize {
    32
}
fn default_per_ip() -> usize {
    4
}
#[derive(Clone, Deserialize)]
pub struct Config {
    pub listen: String,
    pub pool: String,
    pub jds: String,
    pub pool_key: String,
    pub jds_key: String,
    pub identity: String,
    #[serde(default)]
    pub auth: bool,
    #[serde(default)]
    pub user: String,
    #[serde(default)]
    pub password: String,
    #[serde(default = "default_clients")]
    pub max_clients: usize,
    #[serde(default = "default_per_ip")]
    pub max_per_ip: usize,
}
pub fn authority(key: &str) -> Result<[u8; 32]> {
    if key.len() == 64 {
        return model::hash(key);
    }
    let key: key_utils::Secp256k1PublicKey =
        key.to_string().try_into().map_err(|_| protocol_error())?;
    Ok(key.into_bytes())
}
impl Config {
    pub fn validate(&self) -> Result<()> {
        self.listen
            .parse::<SocketAddr>()
            .map_err(|_| protocol_error())?;
        stratum_core::noise_sv2::Initiator::from_raw_k(authority(&self.pool_key)?)
            .map_err(|_| protocol_error())?;
        stratum_core::noise_sv2::Initiator::from_raw_k(authority(&self.jds_key)?)
            .map_err(|_| protocol_error())?;
        for endpoint in [&self.pool, &self.jds] {
            let (host, port) = endpoint.rsplit_once(':').ok_or_else(protocol_error)?;
            if host.is_empty()
                || host.len() > 255
                || host
                    .chars()
                    .any(|c| c.is_control() || c.is_whitespace() || "/@".contains(c))
                || port.parse::<u16>().map_err(|_| protocol_error())? == 0
            {
                return Err(protocol_error());
            }
        }
        if self.identity.is_empty()
            || self.identity.len() > 255
            || self.identity.chars().any(char::is_control)
            || self.pool.is_empty()
            || self.jds.is_empty()
            || !(1..=4096).contains(&self.max_clients)
            || self.max_per_ip == 0
            || self.max_per_ip > self.max_clients
            || (self.auth && (self.user.is_empty() || self.password.is_empty()))
            || self.user.len() > 191
            || self.password.len() > 1024
            || self.user.chars().any(char::is_control)
            || self.password.chars().any(char::is_control)
        {
            return Err(protocol_error());
        }
        Ok(())
    }
}
pub(crate) struct Shared {
    pub stop: AtomicBool,
    status: Mutex<Status>,
}
impl Shared {
    pub(crate) fn update(&self, f: impl FnOnce(&mut Status)) {
        f(&mut self.status.lock().unwrap_or_else(|p| p.into_inner()));
    }
}
pub struct Gateway {
    pub address: SocketAddr,
    shared: Arc<Shared>,
    thread: Option<thread::JoinHandle<()>>,
}
pub fn now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}
pub(crate) async fn stopped(shared: &Shared) {
    while !shared.stop.load(Ordering::Acquire) {
        sleep(Duration::from_millis(20)).await;
    }
}
impl Gateway {
    pub fn start(config: Config, node: Arc<dyn Node>) -> Result<Self> {
        config.validate()?;
        let listener = std::net::TcpListener::bind(&config.listen)?;
        listener.set_nonblocking(true)?;
        let address = listener.local_addr()?;
        let shared = Arc::new(Shared {
            stop: AtomicBool::new(false),
            status: Mutex::new(Status {
                enabled: true,
                listen: address.ip().to_string(),
                port: address.port(),
                auth_required: config.auth,
                status: "connecting".into(),
                session_started: now(),
                ..Status::default()
            }),
        });
        let state = shared.clone();
        let thread = thread::Builder::new()
            .name("purity-sv2".into())
            .spawn(move || {
                let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    let rt = tokio::runtime::Builder::new_current_thread()
                        .enable_all()
                        .build()
                        .map_err(|_| protocol_error())?;
                    rt.block_on(run(config, node, listener, state.clone()));
                    rt.shutdown_timeout(Duration::from_secs(1));
                    Ok::<_, std::io::Error>(())
                }));
                state.update(|s| {
                    s.running = false;
                    s.pending_shares = 0;
                    s.status = "stopped".into();
                    if !matches!(result, Ok(Ok(()))) {
                        s.error = "gateway-runtime-failure".into();
                    }
                });
            })?;
        Ok(Self {
            address,
            shared,
            thread: Some(thread),
        })
    }
    pub fn status(&self) -> Status {
        self.shared
            .status
            .lock()
            .unwrap_or_else(|p| p.into_inner())
            .clone()
    }
    pub fn interrupt(&self) {
        self.shared.stop.store(true, Ordering::Release);
    }
    pub fn stop(mut self) {
        self.interrupt();
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}
impl Drop for Gateway {
    fn drop(&mut self) {
        self.interrupt();
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}
pub(crate) struct Share {
    pub id: Value,
    pub reply: mpsc::Sender<Value>,
    pub job: String,
    pub extranonce: Vec<u8>,
    pub version: u32,
    pub ntime: u32,
    pub nonce: u32,
}
impl Share {
    pub fn answer(&self, accepted: bool) {
        let error = if accepted {
            Value::Null
        } else {
            json!([23, "Share rejected", null])
        };
        let _ = self
            .reply
            .try_send(json!({"id":self.id,"result":accepted,"error":error}));
    }
}
struct Pending {
    share: Share,
    started: Instant,
    difficulty: f64,
    achieved: f64,
}
async fn run(
    config: Config,
    node: Arc<dyn Node>,
    listener: std::net::TcpListener,
    shared: Arc<Shared>,
) {
    let Ok(listener) = TcpListener::from_std(listener) else {
        return;
    };
    let (jobs, job_receiver) = watch::channel(None);
    let (shares, mut receiver) = mpsc::channel(1024);
    let server = tokio::spawn(crate::v1::serve(
        listener,
        config.clone(),
        job_receiver,
        shares,
        shared.clone(),
    ));
    let mut generation = 0;
    let mut delay = 1;
    while !shared.stop.load(Ordering::Acquire) {
        generation += 1;
        shared.update(|s| {
            s.status = "connecting".into();
            s.error.clear();
        });
        tokio::select! {
            _ = stopped(&shared) => break,
            _ = session(&config, &*node, &shared, &jobs, &mut receiver, generation) => {}
        }
        let _ = jobs.send(None);
        shared.update(|s| {
            s.running = false;
            s.status = "retrying".into();
            s.error = "upstream-unavailable-or-protocol-failure".into();
            s.abandoned_shares += s.pending_shares;
            s.pending_shares = 0;
        });
        while let Ok(share) = receiver.try_recv() {
            share.answer(false);
        }
        tokio::select! { _=stopped(&shared)=>break, _=sleep(Duration::from_secs(delay))=>{} }
        delay = (delay * 2).min(30);
    }
    let _ = jobs.send(None);
    server.abort();
    let _ = server.await;
}
macro_rules! decode {
    ($frame:expr, $ty:ty) => {
        binary_sv2::from_bytes::<$ty>(&mut $frame.payload)
            .map_err(|_| protocol_error())?
            .into_owned()
    };
}
async fn setup(
    connection: &mut Connection,
    protocol: Protocol,
    flags: u32,
    endpoint: &str,
) -> Result<()> {
    let (host, port) = endpoint.rsplit_once(':').ok_or_else(protocol_error)?;
    let host = host
        .strip_prefix('[')
        .and_then(|s| s.strip_suffix(']'))
        .unwrap_or(host);
    connection
        .send_owned(Any::Common(Common::SetupConnection(SetupConnectionOwned {
            protocol,
            min_version: 2,
            max_version: 2,
            flags,
            endpoint_host: host
                .as_bytes()
                .to_vec()
                .try_into()
                .map_err(|_| protocol_error())?,
            endpoint_port: port.parse().map_err(|_| protocol_error())?,
            vendor: b"Bitcoin Purity"
                .to_vec()
                .try_into()
                .map_err(|_| protocol_error())?,
            hardware_version: Vec::new().try_into().map_err(|_| protocol_error())?,
            firmware: Vec::new().try_into().map_err(|_| protocol_error())?,
            device_id: Vec::new().try_into().map_err(|_| protocol_error())?,
        })))
        .await?;
    let mut frame = timeout(Duration::from_secs(10), connection.receive())
        .await
        .map_err(|_| protocol_error())??;
    if frame.kind != 1 {
        return Err(protocol_error());
    }
    let success = decode!(frame, SetupConnectionSuccess);
    if success.used_version != 2 || success.flags & flags != flags {
        return Err(protocol_error());
    }
    Ok(())
}
fn request_node(node: &dyn Node, request: Value) -> Result<Value> {
    node.request(request).map_err(|_| protocol_error())
}
async fn session(
    cfg: &Config,
    node: &dyn Node,
    shared: &Shared,
    jobs: &watch::Sender<Option<Arc<Job>>>,
    shares: &mut mpsc::Receiver<Share>,
    generation: u64,
) -> Result<()> {
    let mut pool = Connection::connect(&cfg.pool, authority(&cfg.pool_key)?).await?;
    setup(&mut pool, Protocol::MiningProtocol, 6, &cfg.pool).await?;
    let mut jds = Connection::connect(&cfg.jds, authority(&cfg.jds_key)?).await?;
    setup(&mut jds, Protocol::JobDeclarationProtocol, 1, &cfg.jds).await?;
    pool.send_owned(Any::Mining(Mining::OpenExtendedMiningChannel(
        OpenExtendedMiningChannelOwned {
            request_id: 1,
            user_identity: cfg
                .identity
                .as_bytes()
                .to_vec()
                .try_into()
                .map_err(|_| protocol_error())?,
            nominal_hash_rate: 1e12,
            max_target: [255; 32].into(),
            min_extranonce_size: 8,
        },
    )))
    .await?;
    let mut open = timeout(Duration::from_secs(10), pool.receive())
        .await
        .map_err(|_| protocol_error())??;
    if open.kind != 0x14 {
        return Err(protocol_error());
    }
    let open = decode!(open, OpenExtendedMiningChannelSuccess);
    if open.request_id != 1
        || open.extranonce_size < 8
        || open.extranonce_size as usize + open.extranonce_prefix.len() > 32
    {
        return Err(protocol_error());
    }
    let prefix = open.extranonce_prefix.to_owned_bytes();
    let mut target: [u8; 32] = open
        .target
        .as_bytes()
        .try_into()
        .map_err(|_| protocol_error())?;
    let mut channel = ExtendedChannel::new(
        open.channel_id,
        cfg.identity.clone(),
        ExtranoncePrefix::from_wire(prefix.clone()).map_err(|_| protocol_error())?,
        Target::from_le_bytes(target),
        1e12,
        true,
        open.extranonce_size,
        Some(16),
    )
    .map_err(|_| protocol_error())?;
    let mut tick = interval(Duration::from_secs(1));
    let mut req = 1u32;
    let mut phase = 0u8;
    let mut deadline = Instant::now();
    let mut token: Vec<u8> = Vec::new();
    let mut outputs: Vec<u8> = Vec::new();
    let mut proposed: Option<Job> = None;
    let mut custom: Option<SetCustomMiningJobOwned> = None;
    let mut history: VecDeque<Arc<Job>> = VecDeque::new();
    let mut pending: BTreeMap<u32, Pending> = BTreeMap::new();
    let mut sequence = 0u32;
    let mut force = false;
    loop {
        tokio::select! {
            _ = tick.tick() => {
                if phase != 0 && phase != 2 && deadline.elapsed() > Duration::from_secs(15) {return Err(protocol_error());}
                if pending.values().any(|p|p.started.elapsed()>Duration::from_secs(30)) {return Err(protocol_error());}
                if phase == 0 || phase == 2 {
                    if !outputs.is_empty() {
                        let mut query=json!({"op":"template","outputs":hex::encode(&outputs)});
                        if phase==0 && !force {
                            if let Some(job)=history.back() {query["known_id"]=json!(job.template.id);}
                        }
                        let value = request_node(node,query)?;
                        if value["unchanged"]==true {continue;}
                        if value["ok"] == true {
                            let template:Template = serde_json::from_value(value).map_err(|_|protocol_error())?;
                            if phase == 0 {
                                if !force && history.back().is_some_and(|j|j.template.id==template.id) {continue;}
                                if history.back().is_some_and(|j|j.template.previous_block_hash != template.previous_block_hash) {
                                    let _ = jobs.send(None);
                                    shared.update(|s|s.running=false);
                                }
                            } else {
                                let job = Job::prepare(template,&outputs,prefix.clone(),open.extranonce_size as usize,open.channel_id,generation,target)?;
                                let check = request_node(node,job.node_request("validate",&vec![0;job.rollable_size],job.template.version,job.template.ntime,0)?)?;
                                if check["ok"] != true {return Err(protocol_error());}
                                jds.send_owned(Any::JobDeclaration(Jd::DeclareMiningJob(DeclareMiningJobOwned {
                                    request_id:req,mining_job_token:token.clone().try_into().map_err(|_|protocol_error())?,
                                    version:job.template.version,
                                    coinbase_tx_prefix:job.full_prefix.clone().try_into().map_err(|_|protocol_error())?,
                                    coinbase_tx_suffix:job.full_suffix.clone().try_into().map_err(|_|protocol_error())?,
                                    wtxid_list:job.template.wtxids.iter().map(|s|model::uint256(s)).collect::<Result<Vec<_>>>()?.try_into().map_err(|_|protocol_error())?,
                                    excess_data:Vec::new().try_into().map_err(|_|protocol_error())?,
                                }))).await?;
                                proposed = Some(job); phase=3;deadline=Instant::now();continue;
                            }
                        } else {
                            if !matches!(value["error"].as_str(),Some("not-ready"|"initial-sync"|"catching-up"|"tip-changed")) {return Err(protocol_error());}
                            force=true;let _ = jobs.send(None); shared.update(|s|{s.running=false;s.status="waiting-for-local-template".into();});continue;
                        }
                    }
                    req = req.checked_add(1).ok_or_else(protocol_error)?;
                    jds.send_owned(Any::JobDeclaration(Jd::AllocateMiningJobToken(AllocateMiningJobTokenOwned {
                        user_identifier:cfg.identity.as_bytes().to_vec().try_into().map_err(|_|protocol_error())?,request_id:req,
                    }))).await?;
                    phase=1;deadline=Instant::now();shared.update(|s|s.status="declaring".into());
                }
            }
            frame = jds.receive() => {
                let mut frame=frame?;
                match frame.kind {
                    0x51 if phase==1 => {
                        let allocated=decode!(frame,AllocateMiningJobTokenSuccess);
                        if allocated.request_id != req {return Err(protocol_error());}
                        token=allocated.mining_job_token.to_owned_bytes();outputs=allocated.coinbase_outputs.to_owned_bytes();
                        phase=2;
                    }
                    0x55 if phase==3 => {
                        let missing=decode!(frame,ProvideMissingTransactions);
                        if missing.request_id != req {return Err(protocol_error());}
                        let job=proposed.as_ref().ok_or_else(protocol_error)?;
                        let positions=missing.unknown_tx_position_list.into_inner();
                        if positions.len()>job.template.transactions.len() {return Err(protocol_error());}
                        let mut seen=BTreeSet::new();
                        let txs=positions.into_iter().map(|i| {
                            if !seen.insert(i) {return Err(protocol_error());}
                            let tx=job.template.transactions.get(i as usize).ok_or_else(protocol_error)?;
                            model::bytes(tx)?.try_into().map_err(|_|protocol_error())
                        }).collect::<Result<Vec<binary_sv2::B016MOwned>>>()?;
                        jds.send_owned(Any::JobDeclaration(Jd::ProvideMissingTransactionsSuccess(ProvideMissingTransactionsSuccessOwned {
                            request_id:req,transaction_list:txs.try_into().map_err(|_|protocol_error())?,
                        }))).await?;
                    }
                    0x58 if phase==3 => {
                        let declared=decode!(frame,DeclareMiningJobSuccess);
                        if declared.request_id!=req {return Err(protocol_error());}
                        let job=proposed.as_ref().ok_or_else(protocol_error)?;
                        let message=job.custom(req,declared.new_mining_job_token.to_owned_bytes())?;
                        pool.send_owned(Any::Mining(Mining::SetCustomMiningJob(message.clone()))).await?;
                        custom=Some(message);phase=4;deadline=Instant::now();
                    }
                    _=>return Err(protocol_error())
                }
            }
            frame = pool.receive() => {
                let mut frame=frame?;
                match frame.kind {
                    0x23 if phase==4 => {
                        let success=decode!(frame,SetCustomMiningJobSuccess);
                        let message=custom.take().ok_or_else(protocol_error)?;
                        if success.request_id!=req || success.channel_id!=open.channel_id {return Err(protocol_error());}
                        if history.iter().any(|j|j.upstream_id==success.job_id) {return Err(protocol_error());}
                        let mut job=proposed.take().ok_or_else(protocol_error)?;
                        job.target=target;
                        channel.on_chain_tip_update(ChainTip::new(model::uint256(&job.template.previous_block_hash)?,job.template.nbits,job.template.ntime)).map_err(|_|protocol_error())?;
                        channel.on_set_custom_mining_job_success(message,success.clone()).map_err(|_|protocol_error())?;
                        job.activate(success.job_id)?;
                        let job=Arc::new(job);
                        shared.update(|s|{s.running=true;s.status="running".into();s.error.clear();s.current_height=job.template.height;
                            s.job_id=job.id();s.job_created=now();s.nbits=job.template.nbits;
                            s.share_difficulty=Target::from_le_bytes(job.target).difficulty_float();
                            s.network_difficulty=Target::from_compact(stratum_core::bitcoin::CompactTarget::from_consensus(job.template.nbits)).difficulty_float();
                            s.previous_block_hash=stratum_core::bitcoin::BlockHash::from_byte_array(model::hash(&job.template.previous_block_hash).unwrap()).to_string();
                            s.transactions=job.template.transactions.len() as u64;s.template_size=job.template.size;s.template_weight=job.template.weight;s.coinbase_value=job.template.reward;});
                        history.push_back(job.clone());while history.len()>16 {history.pop_front();}
                        let _=jobs.send(Some(job));phase=0;force=false;
                    }
                    0x21 => {
                        let update=decode!(frame,SetTarget);
                        if update.channel_id!=open.channel_id {return Err(protocol_error());}
                        target=update.target.as_bytes().try_into().map_err(|_|protocol_error())?;
                        channel.set_target(Target::from_le_bytes(target)).map_err(|_|protocol_error())?;force=true;
                    }
                    0x1c => {
                        let accepted=decode!(frame,SubmitSharesSuccess);
                        if accepted.channel_id!=open.channel_id {return Err(protocol_error());}
                        let keys:Vec<u32>=pending.range(..=accepted.last_sequence_number).map(|(s,_)|*s).collect();
                        if keys.len()!=accepted.new_submits_accepted_count as usize {return Err(protocol_error());}
                        for key in keys {
                            if let Some(p)=pending.remove(&key) {
                                p.share.answer(true);
                                shared.update(|s|{s.accepted_shares+=1;s.accepted_difficulty+=p.difficulty;s.best_share_difficulty=s.best_share_difficulty.max(p.achieved);s.last_share_time=now();});
                            }
                        }
                        shared.update(|s|s.pending_shares=pending.len() as u64);
                    }
                    0x1d => {
                        let rejected=decode!(frame,SubmitSharesError);
                        if rejected.channel_id!=open.channel_id {return Err(protocol_error());}
                        let p=pending.remove(&rejected.sequence_number).ok_or_else(protocol_error)?;
                        p.share.answer(false);shared.update(|s|{s.rejected_shares+=1;s.pending_shares=pending.len() as u64;});
                    }
                    0x15|0x1f|0x20|0x25=>{}
                    _=>return Err(protocol_error())
                }
            }
            Some(share)=shares.recv()=> {
                let Some(job)=history.iter().find(|j|j.id()==share.job) else {share.answer(false);continue};
                if pending.len()>=1024 || (share.version & !0x1fffe000)!=(job.template.version & !0x1fffe000) {
                    share.answer(false);continue;
                }
                sequence=sequence.checked_add(1).ok_or_else(protocol_error)?;
                let message=SubmitSharesExtendedOwned {channel_id:open.channel_id,sequence_number:sequence,job_id:job.upstream_id,
                    nonce:share.nonce,ntime:share.ntime,version:share.version,
                    extranonce:share.extranonce.clone().try_into().map_err(|_|protocol_error())?};
                let validation=match channel.validate_share(message.clone()) {
                    Ok(v)=>v,Err(error)=>{
                        let reason=match error {
                            ShareValidationError::Invalid(code)|ShareValidationError::Stale(code)|
                            ShareValidationError::InvalidJobId(code)|ShareValidationError::DoesNotMeetTarget(code)|
                            ShareValidationError::VersionRollingNotAllowed(code)|ShareValidationError::DuplicateShare(code)|
                            ShareValidationError::BadExtranonceSize(code)=>code,
                            ShareValidationError::NoChainTip=>"missing-chain-tip",
                        };
                        share.answer(false);shared.update(|s|{s.rejected_shares+=1;s.error=reason.into();});continue
                    }
                };
                let hash=match validation {ShareValidationResult::Valid(h)|ShareValidationResult::BlockFound(h)=>h};
                shared.update(|s| s.local_valid_shares+=1);
                if matches!(validation,ShareValidationResult::BlockFound(_)) {
                    let response=request_node(node,job.node_request("submit",&share.extranonce,share.version,share.ntime,share.nonce)?)?;
                    shared.update(|s|{
                        s.block_candidates+=1;
                        s.last_block_hash=hash.to_string();s.last_block_time=now();
                        s.last_block_result=if response["ok"]==true {
                            s.blocks_accepted+=1;"accepted"
                        }else{
                            s.blocks_rejected+=1;
                            match response["error"].as_str() {
                                Some("duplicate")=>"duplicate",
                                Some("duplicate-invalid")=>"duplicate-invalid",
                                Some("inconclusive")=>"inconclusive",
                                _=>"rejected",
                            }
                        }.into();
                    });
                    let mut full=job.extranonce_prefix.clone();full.extend_from_slice(&share.extranonce);
                    jds.send_owned(Any::JobDeclaration(Jd::PushSolution(PushSolutionOwned {
                        extranonce:full.try_into().map_err(|_|protocol_error())?,
                        prev_hash:model::uint256(&job.template.previous_block_hash)?,nonce:share.nonce,ntime:share.ntime,nbits:job.template.nbits,version:share.version,
                    }))).await?;
                }
                pool.send_owned(Any::Mining(Mining::SubmitSharesExtended(message))).await?;
                pending.insert(sequence,Pending {share,started:Instant::now(),difficulty:Target::from_le_bytes(job.target).difficulty_float(),achieved:Target::from_le_bytes(hash.to_byte_array()).difficulty_float()});
                shared.update(|s|s.pending_shares=pending.len() as u64);
            }
        }
    }
}
