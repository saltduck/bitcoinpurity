// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
use crate::wire::{protocol_error, Result};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use stratum_core::{
    binary_sv2::Sv2OptionOwned,
    bitcoin::{
        consensus::{deserialize, serialize},
        Transaction, TxOut,
    },
    mining_sv2::{NewExtendedMiningJobOwned, SetCustomMiningJobOwned, SetNewPrevHashOwned},
    stratum_translation::sv2_to_sv1::build_sv1_notify_from_sv2,
};

#[derive(Clone, Deserialize)]
pub struct Template {
    pub size: u64,
    pub weight: u64,
    pub reward: u64,
    pub id: u64,
    pub height: u64,
    pub version: u32,
    pub ntime: u32,
    pub nbits: u32,
    pub previous_block_hash: String,
    pub coinbase: String,
    pub coinbase_prefix: String,
    pub transactions: Vec<String>,
    pub wtxids: Vec<String>,
    pub merkle_path: Vec<String>,
}
#[derive(Clone)]
pub struct Job {
    pub template: Template,
    pub generation: u64,
    pub upstream_id: u32,
    pub channel_id: u32,
    pub extranonce_prefix: Vec<u8>,
    pub rollable_size: usize,
    pub target: [u8; 32],
    pub coinbase: Transaction,
    pub full_prefix: Vec<u8>,
    pub full_suffix: Vec<u8>,
    pub notify: Value,
}
pub fn bytes(s: &str) -> Result<Vec<u8>> {
    hex::decode(s).map_err(|_| protocol_error())
}
pub fn hash(s: &str) -> Result<[u8; 32]> {
    bytes(s)?.try_into().map_err(|_| protocol_error())
}
pub fn uint256(s: &str) -> Result<stratum_core::binary_sv2::U256Owned> {
    Ok(hash(s)?.into())
}
impl Template {
    pub fn add_coinbase_tags(&mut self, pool_tag: &str, miner_tag: &str) -> Result<()> {
        let mut prefix = bytes(&self.coinbase_prefix)?;
        for tag in [pool_tag, miner_tag] {
            if tag.is_empty() {
                continue;
            }
            if tag.len() > 75 || tag.chars().any(char::is_control) {
                return Err(protocol_error());
            }
            prefix.push(tag.len() as u8);
            prefix.extend_from_slice(tag.as_bytes());
        }
        self.coinbase_prefix = hex::encode(prefix);
        Ok(())
    }
}
impl Job {
    pub fn id(&self) -> String {
        format!(
            "{}:{}:{}",
            self.generation, self.template.id, self.upstream_id
        )
    }
    pub fn prepare(
        mut template: Template,
        outputs: &[u8],
        prefix: Vec<u8>,
        rollable: usize,
        channel_id: u32,
        generation: u64,
        target: [u8; 32],
    ) -> Result<Self> {
        let mut coinbase: Transaction =
            deserialize(&bytes(&template.coinbase)?).map_err(|_| protocol_error())?;
        let mut pool_outputs: Vec<TxOut> = deserialize(outputs).map_err(|_| protocol_error())?;
        if pool_outputs.is_empty()
            || pool_outputs.iter().any(|o| o.value.to_sat() != 0)
            || coinbase.input.len() != 1
            || coinbase.output.is_empty()
            || rollable < 8
            || rollable + prefix.len() > 32
        {
            return Err(protocol_error());
        }
        let mut sig_prefix = bytes(&template.coinbase_prefix)?;
        if sig_prefix.is_empty() || sig_prefix.len() > 67 {
            return Err(protocol_error());
        }
        if sig_prefix.len() + 1 + prefix.len() + rollable > 100 { return Err(protocol_error()); }
        sig_prefix.push((prefix.len() + rollable) as u8);
        template.coinbase_prefix = hex::encode(&sig_prefix);
        pool_outputs[0].value = coinbase.output[0].value;
        pool_outputs.extend_from_slice(&coinbase.output[1..]);
        coinbase.output = pool_outputs;
        let mut script = sig_prefix.clone();
        script.resize(script.len() + prefix.len() + rollable, 0);
        coinbase.input[0].script_sig = script.into();
        if coinbase.input[0].witness.to_vec() != vec![vec![0; 32]] {
            return Err(protocol_error());
        }
        let serialized = serialize(&coinbase);
        let split = 44 + sig_prefix.len();
        let suffix = split + prefix.len() + rollable;
        if suffix > serialized.len() {
            return Err(protocol_error());
        }
        Ok(Self {
            template,
            generation,
            upstream_id: 0,
            channel_id,
            extranonce_prefix: prefix,
            rollable_size: rollable,
            target,
            full_prefix: serialized[..split].to_vec(),
            full_suffix: serialized[suffix..].to_vec(),
            coinbase,
            notify: Value::Null,
        })
    }
    pub fn custom(&self, request_id: u32, token: Vec<u8>) -> Result<SetCustomMiningJobOwned> {
        Ok(SetCustomMiningJobOwned {
            channel_id: self.channel_id,
            request_id,
            token: token.try_into().map_err(|_| protocol_error())?,
            version: self.template.version,
            prev_hash: uint256(&self.template.previous_block_hash)?,
            ntime_start: self.template.ntime,
            nbits: self.template.nbits,
            coinbase_tx_version: self.coinbase.version.0 as u32,
            coinbase_prefix: bytes(&self.template.coinbase_prefix)?
                .try_into()
                .map_err(|_| protocol_error())?,
            coinbase_tx_input_n_sequence: self.coinbase.input[0].sequence.0,
            coinbase_tx_outputs: serialize(&self.coinbase.output)
                .try_into()
                .map_err(|_| protocol_error())?,
            coinbase_tx_locktime: self.coinbase.lock_time.to_consensus_u32(),
            merkle_path: self
                .template
                .merkle_path
                .iter()
                .map(|s| uint256(s))
                .collect::<Result<Vec<_>>>()?
                .try_into()
                .map_err(|_| protocol_error())?,
        })
    }
    pub fn activate(&mut self, upstream_id: u32, clean_jobs: bool) -> Result<()> {
        self.upstream_id = upstream_id;
        let job = self.mining_job()?;
        let prev = SetNewPrevHashOwned {
            channel_id: self.channel_id,
            job_id: upstream_id,
            prev_hash: uint256(&self.template.previous_block_hash)?,
            ntime_start: self.template.ntime,
            nbits: self.template.nbits,
        };
        let notify =
            build_sv1_notify_from_sv2(prev, job, clean_jobs).map_err(|_| protocol_error())?;
        let message: stratum_core::sv1_api::json_rpc::Message = notify.into();
        self.notify = serde_json::to_value(message).map_err(|_| protocol_error())?;
        self.notify["params"][0] = json!(self.id());
        Ok(())
    }
    pub fn mining_job(&self) -> Result<NewExtendedMiningJobOwned> {
        Ok(NewExtendedMiningJobOwned {
            channel_id: self.channel_id,
            job_id: self.upstream_id,
            ntime_start: Sv2OptionOwned::new(Some(self.template.ntime)),
            version: self.template.version,
            version_rolling_allowed: true,
            merkle_path: self
                .template
                .merkle_path
                .iter()
                .map(|s| uint256(s))
                .collect::<Result<Vec<_>>>()?
                .try_into()
                .map_err(|_| protocol_error())?,
            coinbase_tx_prefix: self
                .full_prefix
                .clone()
                .try_into()
                .map_err(|_| protocol_error())?,
            coinbase_tx_suffix: self
                .full_suffix
                .clone()
                .try_into()
                .map_err(|_| protocol_error())?,
        })
    }
    pub fn complete_coinbase(&self, extranonce: &[u8]) -> Result<Transaction> {
        if extranonce.len() != self.rollable_size {
            return Err(protocol_error());
        }
        let mut data = self.full_prefix.clone();
        data.extend_from_slice(&self.extranonce_prefix);
        data.extend_from_slice(extranonce);
        data.extend_from_slice(&self.full_suffix);
        deserialize(&data).map_err(|_| protocol_error())
    }
    pub fn node_request(
        &self,
        op: &str,
        extranonce: &[u8],
        version: u32,
        ntime: u32,
        nonce: u32,
    ) -> Result<Value> {
        Ok(
            json!({"op":op,"id":self.template.id,"coinbase":hex::encode(serialize(&self.complete_coinbase(extranonce)?)),
            "version":version,"ntime":ntime,"nonce":nonce}),
        )
    }
}
#[derive(Default, Serialize, Clone)]
pub struct Status {
    pub enabled: bool,
    pub running: bool,
    pub status: String,
    pub stage: String,
    pub error: String,
    pub listen: String,
    pub port: u16,
    pub auth_required: bool,
    pub subscribed_clients: u32,
    pub clients: u32,
    pub authorized_clients: u32,
    pub local_valid_shares: u64,
    pub accepted_shares: u64,
    pub rejected_shares: u64,
    pub pending_shares: u64,
    pub accepted_difficulty: f64,
    pub best_share_difficulty: f64,
    pub session_started: u64,
    pub last_share_time: u64,
    pub current_height: u64,
    pub abandoned_shares: u64,
    pub job_id: String,
    pub job_created: u64,
    pub previous_block_hash: String,
    pub nbits: u32,
    pub network_difficulty: f64,
    pub share_difficulty: f64,
    pub transactions: u64,
    pub template_size: u64,
    pub template_weight: u64,
    pub coinbase_value: u64,
    pub block_candidates: u64,
    pub blocks_accepted: u64,
    pub blocks_rejected: u64,
    pub last_block_time: u64,
    pub last_block_hash: String,
    pub last_block_result: String,
}
