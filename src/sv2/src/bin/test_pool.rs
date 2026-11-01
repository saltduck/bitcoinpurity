// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
// Regtest-only protocol fixture. Never deploy as a public mining pool.
use purity_sv2::wire::{protocol_error, Connection, Result};
use serde::Deserialize;
use serde_json::json;
use std::{
    collections::BTreeMap,
    fs::OpenOptions,
    io::Write,
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc, Mutex,
    },
};
use stratum_core::{
    binary_sv2,
    bitcoin::{
        self,
        consensus::{deserialize, serialize},
        hashes::Hash,
        secp256k1::{Keypair, Secp256k1, SecretKey},
        Amount, ScriptBuf, Target, Transaction, TxOut,
    },
    channels_sv2::{
        extranonce_manager::ExtranonceAllocator,
        server::{extended::ExtendedChannel, share_accounting::ShareValidationResult},
    },
    common_messages_sv2::*,
    job_declaration_sv2::*,
    mining_sv2::*,
    parsers_sv2::{
        AnyMessageOwned as Any, CommonMessagesOwned as Common, JobDeclarationOwned as Jd,
        MiningOwned as Mining,
    },
};
use tokio::net::TcpListener;
#[derive(Clone, Deserialize)]
struct Config {
    mining: String,
    jds: String,
    events: String,
    #[serde(default)]
    mode: String,
}
#[derive(Clone)]
struct Declaration {
    message: DeclareMiningJobOwned,
    transactions: Vec<Transaction>,
}
type Records = Arc<Mutex<BTreeMap<Vec<u8>, Declaration>>>;
macro_rules! decode {
    ($frame:expr,$ty:ty) => {
        binary_sv2::from_bytes::<$ty>(&mut $frame.payload)
            .map_err(|_| protocol_error())?
            .into_owned()
    };
}
fn event(cfg: &Config, value: serde_json::Value) {
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&cfg.events)
        .unwrap();
    writeln!(file, "{value}").unwrap();
}
fn mode(path: &str) -> String {
    std::fs::read(path)
        .ok()
        .and_then(|b| serde_json::from_slice::<Config>(&b).ok())
        .map(|c| c.mode)
        .unwrap_or_default()
}
async fn job_server(
    mut connection: Connection,
    cfg: Config,
    path: String,
    records: Records,
    counter: Arc<AtomicU64>,
) -> Result<()> {
    let mut requested: Option<DeclareMiningJobOwned> = None;
    loop {
        let mut frame = connection.receive().await?;
        match frame.kind {
            0x50 => {
                let request = decode!(frame, AllocateMiningJobToken);
                let token = counter
                    .fetch_add(1, Ordering::Relaxed)
                    .to_le_bytes()
                    .to_vec();
                connection
                    .send_owned(Any::JobDeclaration(Jd::AllocateMiningJobTokenSuccess(
                        AllocateMiningJobTokenSuccessOwned {
                            request_id: request.request_id,
                            mining_job_token: token.try_into().map_err(|_| protocol_error())?,
                            coinbase_outputs: serialize(&vec![TxOut {
                                value: Amount::ZERO,
                                script_pubkey: ScriptBuf::from_bytes(vec![0x51]),
                            }])
                            .try_into()
                            .map_err(|_| protocol_error())?,
                        },
                    )))
                    .await?;
            }
            0x57 => {
                let request = decode!(frame, DeclareMiningJob);
                event(
                    &cfg,
                    json!({"event":"declared","transactions":request.wtxid_list.len()}),
                );
                if mode(&path) == "reject-job" {
                    connection
                        .send_owned(Any::JobDeclaration(Jd::DeclareMiningJobError(
                            DeclareMiningJobErrorOwned {
                                request_id: request.request_id,
                                error_code: b"test-rejection".to_vec().try_into().unwrap(),
                                error_details: Vec::new().try_into().unwrap(),
                            },
                        )))
                        .await?;
                    continue;
                }
                if mode(&path) == "silent-job" {
                    continue;
                }
                let indices: Vec<u16> = if mode(&path) == "duplicate-missing" {
                    event(
                        &cfg,
                        json!({"event":"duplicate-missing-request","transactions":request.wtxid_list.len()}),
                    );
                    vec![0, 0]
                } else {
                    (0..request.wtxid_list.len()).map(|n| n as u16).collect()
                };
                connection
                    .send_owned(Any::JobDeclaration(Jd::ProvideMissingTransactions(
                        ProvideMissingTransactionsOwned {
                            request_id: request.request_id,
                            unknown_tx_position_list: indices
                                .try_into()
                                .map_err(|_| protocol_error())?,
                        },
                    )))
                    .await?;
                requested = Some(request);
            }
            0x56 => {
                let response = decode!(frame, ProvideMissingTransactionsSuccess);
                if mode(&path) == "duplicate-missing" {
                    event(&cfg, json!({"event":"unexpected-duplicate-response"}));
                    return Err(protocol_error());
                }
                let request = requested.take().ok_or_else(protocol_error)?;
                if request.request_id != response.request_id {
                    return Err(protocol_error());
                }
                let transactions: Vec<Transaction> = response
                    .transaction_list
                    .into_inner()
                    .into_iter()
                    .map(|b| deserialize(b.as_bytes()).map_err(|_| protocol_error()))
                    .collect::<Result<_>>()?;
                if transactions.len() != request.wtxid_list.len() {
                    return Err(protocol_error());
                }
                for (tx, id) in transactions
                    .iter()
                    .zip(request.wtxid_list.clone().into_inner())
                {
                    if tx.compute_wtxid().to_byte_array().as_slice() != id.as_bytes() {
                        return Err(protocol_error());
                    }
                }
                let token = request.mining_job_token.to_owned_bytes();
                records.lock().unwrap().insert(
                    token.clone(),
                    Declaration {
                        message: request,
                        transactions,
                    },
                );
                while records.lock().unwrap().len() > 32 {
                    records.lock().unwrap().pop_first();
                }
                event(&cfg, json!({"event":"missing-transactions-provided"}));
                connection
                    .send_owned(Any::JobDeclaration(Jd::DeclareMiningJobSuccess(
                        DeclareMiningJobSuccessOwned {
                            request_id: response.request_id,
                            new_mining_job_token: token.try_into().map_err(|_| protocol_error())?,
                        },
                    )))
                    .await?;
            }
            0x60 => {
                let _ = decode!(frame, PushSolution);
                event(&cfg, json!({"event":"push-solution"}));
            }
            _ => return Err(protocol_error()),
        }
    }
}
async fn mining_server(
    mut connection: Connection,
    cfg: Config,
    path: String,
    records: Records,
) -> Result<()> {
    let mut frame = connection.receive().await?;
    if frame.kind != 0x13 {
        return Err(protocol_error());
    }
    let open = decode!(frame, OpenExtendedMiningChannel);
    let mut allocator =
        ExtranonceAllocator::new(vec![0x50], 16, 32).map_err(|_| protocol_error())?;
    let prefix = allocator
        .allocate_extended(open.min_extranonce_size as usize)
        .map_err(|_| protocol_error())?;
    let rollable = 16 - prefix.as_bytes().len();
    let mut channel = ExtendedChannel::new_for_pool(
        1,
        "regtest".into(),
        prefix,
        Target::from_le_bytes([255; 32]),
        1e12,
        true,
        rollable as u16,
        1,
        6.0,
        "test".into(),
        Some(16),
    )
    .map_err(|_| protocol_error())?;
    let share_target = if mode(&path) == "strict-target" {
        Target::MAX
    } else {
        Target::from_le_bytes([255; 32])
    };
    channel
        .set_target(share_target)
        .map_err(|_| protocol_error())?;
    connection
        .send_owned(Any::Mining(Mining::OpenExtendedMiningChannelSuccess(
            OpenExtendedMiningChannelSuccessOwned {
                request_id: open.request_id,
                channel_id: 1,
                target: share_target.to_le_bytes().into(),
                extranonce_size: rollable as u16,
                extranonce_prefix: channel
                    .get_extranonce_prefix()
                    .to_vec()
                    .try_into()
                    .map_err(|_| protocol_error())?,
                group_channel_id: 0,
            },
        )))
        .await?;
    let mut declared: BTreeMap<u32, (SetCustomMiningJobOwned, Declaration)> = BTreeMap::new();
    loop {
        let mut frame = connection.receive().await?;
        match frame.kind {
            0x22 => {
                let custom = decode!(frame, SetCustomMiningJob);
                let record = records
                    .lock()
                    .unwrap()
                    .get(custom.token.as_bytes())
                    .cloned()
                    .ok_or_else(protocol_error)?;
                if record.message.version != custom.version {
                    return Err(protocol_error());
                }
                let id = channel
                    .on_set_custom_mining_job(custom.clone())
                    .map_err(|_| protocol_error())?;
                let request_id = custom.request_id;
                declared.insert(id, (custom, record));
                while declared.len() > 16 {
                    declared.pop_first();
                }
                connection
                    .send_owned(Any::Mining(Mining::SetCustomMiningJobSuccess(
                        SetCustomMiningJobSuccess {
                            channel_id: 1,
                            request_id,
                            job_id: id,
                        },
                    )))
                    .await?;
                event(&cfg, json!({"event":"custom-job-accepted"}));
            }
            0x1b => {
                let share = decode!(frame, SubmitSharesExtended);
                if mode(&path) == "close" {
                    return Err(protocol_error());
                }
                let result = channel.validate_share(share.clone());
                if mode(&path) == "reject-share" || result.is_err() {
                    connection
                        .send_owned(Any::Mining(Mining::SubmitSharesError(
                            SubmitSharesErrorOwned {
                                channel_id: 1,
                                sequence_number: share.sequence_number,
                                error_code: b"rejected".to_vec().try_into().unwrap(),
                            },
                        )))
                        .await?;
                    continue;
                }
                if let Ok(ShareValidationResult::BlockFound(hash, _, coinbase)) = result {
                    let (custom, record) =
                        declared.get(&share.job_id).ok_or_else(protocol_error)?;
                    let mut transactions =
                        vec![deserialize::<Transaction>(&coinbase).map_err(|_| protocol_error())?];
                    transactions.extend(record.transactions.clone());
                    let mut block = bitcoin::Block {
                        header: bitcoin::block::Header {
                            version: bitcoin::block::Version::from_consensus(share.version as i32),
                            prev_blockhash: bitcoin::BlockHash::from_byte_array(
                                custom.prev_hash.as_bytes().try_into().unwrap(),
                            ),
                            merkle_root: bitcoin::TxMerkleNode::all_zeros(),
                            time: share.ntime,
                            bits: bitcoin::CompactTarget::from_consensus(custom.nbits),
                            nonce: share.nonce,
                        },
                        txdata: transactions,
                    };
                    block.header.merkle_root =
                        block.compute_merkle_root().ok_or_else(protocol_error)?;
                    if block.block_hash().to_byte_array() != hash.to_byte_array() {
                        return Err(protocol_error());
                    }
                    event(
                        &cfg,
                        json!({"event":"block","hex":hex::encode(serialize(&block))}),
                    );
                }
                if mode(&path) == "delay-ack" {
                    continue;
                }
                connection
                    .send_owned(Any::Mining(Mining::SubmitSharesSuccess(
                        SubmitSharesSuccess {
                            channel_id: 1,
                            last_sequence_number: share.sequence_number,
                            new_submits_accepted_count: 1,
                            new_shares_sum: 1,
                        },
                    )))
                    .await?;
                event(&cfg, json!({"event":"share-accepted"}));
            }
            _ => return Err(protocol_error()),
        }
    }
}
#[tokio::main(flavor = "current_thread")]
async fn main() {
    let path = std::env::args()
        .nth(1)
        .expect("regtest fixture config required");
    let cfg: Config = serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
    let secret = [1u8; 32];
    let pair =
        Keypair::from_secret_key(&Secp256k1::new(), &SecretKey::from_slice(&secret).unwrap());
    let public = pair.x_only_public_key().0.serialize();
    let mining = TcpListener::bind(&cfg.mining).await.unwrap();
    let jds = TcpListener::bind(&cfg.jds).await.unwrap();
    let records: Records = Arc::new(Mutex::new(BTreeMap::new()));
    let counter = Arc::new(AtomicU64::new(1));
    println!(
        "{}",
        json!({"public_key":hex::encode(public),"mining":mining.local_addr().unwrap().to_string(),"jds":jds.local_addr().unwrap().to_string()})
    );
    loop {
        let (socket, is_jds) = tokio::select! {
            s=mining.accept()=>{(s.unwrap().0,false)}
            s=jds.accept()=>{(s.unwrap().0,true)}
        };
        let cfg = cfg.clone();
        let path = path.clone();
        let records = records.clone();
        let counter = counter.clone();
        tokio::spawn(async move {
            let result = async {
                let mut connection = Connection::respond(socket, public, secret).await?;
                let mut frame = connection.receive().await?;
                if frame.kind != 0 {
                    return Err(protocol_error());
                }
                let setup = decode!(frame, SetupConnection);
                let expected = if is_jds {
                    Protocol::JobDeclarationProtocol
                } else {
                    Protocol::MiningProtocol
                };
                let endpoint = if is_jds { &cfg.jds } else { &cfg.mining };
                let (host, port) = endpoint.rsplit_once(':').ok_or_else(protocol_error)?;
                if setup.protocol != expected
                    || setup.endpoint_host.as_bytes() != host.as_bytes()
                    || setup.endpoint_port != port.parse::<u16>().map_err(|_| protocol_error())?
                {
                    return Err(protocol_error());
                }
                connection
                    .send_owned(Any::Common(Common::SetupConnectionSuccess(
                        SetupConnectionSuccess {
                            used_version: if mode(&path) == "bad-version" { 1 } else { 2 },
                            flags: if mode(&path) == "missing-features" {
                                0
                            } else {
                                setup.flags
                            },
                        },
                    )))
                    .await?;
                if is_jds {
                    job_server(connection, cfg.clone(), path, records, counter).await
                } else {
                    mining_server(connection, cfg.clone(), path, records).await
                }
            }
            .await;
            if result.is_err() {
                event(&cfg, json!({"event":"connection-closed"}));
            }
        });
    }
}
