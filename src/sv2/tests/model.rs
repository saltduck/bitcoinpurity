use purity_sv2::model::{Job, Template};
use stratum_core::bitcoin::{
    absolute, consensus::serialize, transaction, Amount, OutPoint, ScriptBuf, Sequence,
    Transaction, TxIn, TxOut, Witness,
};

fn template(prefix: Vec<u8>) -> Template {
    let mut witness = Witness::new();
    witness.push([0u8; 32]);
    let coinbase = Transaction {
        version: transaction::Version::TWO,
        lock_time: absolute::LockTime::ZERO,
        input: vec![TxIn {
            previous_output: OutPoint::null(),
            script_sig: ScriptBuf::from_bytes(vec![3, 0x40, 0x0d, 3, 0]),
            sequence: Sequence::MAX,
            witness,
        }],
        output: vec![
            TxOut {
                value: Amount::from_sat(500),
                script_pubkey: ScriptBuf::new(),
            },
            TxOut {
                value: Amount::ZERO,
                script_pubkey: ScriptBuf::from_bytes(vec![0x6a, 1, 42]),
            },
        ],
    };
    serde_json::from_value(
        serde_json::json!({"size":200,"weight":800,"reward":500,"id":1,"height":200000,
        "version":536870912,"ntime":1000,"nbits":545259519,"previous_block_hash":"11".repeat(32),
        "coinbase":hex::encode(serialize(&coinbase)),"coinbase_prefix":hex::encode(prefix),
        "transactions":[],"wtxids":[],"merkle_path":[]}),
    )
    .unwrap()
}

fn prepare(prefix: Vec<u8>) -> std::io::Result<Job> {
    Job::prepare(
        template(prefix),
        &serialize(&vec![TxOut {
            value: Amount::ZERO,
            script_pubkey: ScriptBuf::from_bytes(vec![0x51]),
        }]),
        vec![1, 2, 3, 4],
        8,
        1,
        1,
        [255; 32],
    )
}

#[test]
fn extranonce_is_a_final_push_and_custom_matches_declared_script() {
    let job = prepare(vec![3, 0x40, 0x0d, 3]).unwrap();
    let script = job.coinbase.input[0].script_sig.as_bytes();
    assert_eq!(&script[..5], &[3, 0x40, 0x0d, 3, 12]);
    assert_eq!(job.full_prefix.last(), Some(&12));
    assert_eq!(
        job.custom(1, vec![1; 32])
            .unwrap()
            .coinbase_prefix
            .as_bytes(),
        &script[..script.len() - 12]
    );
    let complete = job.complete_coinbase(&[0xff; 8]).unwrap();
    assert_eq!(
        &complete.input[0].script_sig.as_bytes()[5..],
        &[1, 2, 3, 4, 255, 255, 255, 255, 255, 255, 255, 255]
    );
    assert_eq!(complete.output[0].value.to_sat(), 500);
    assert_eq!(complete.output[0].script_pubkey.as_bytes(), &[0x51]);
    assert_eq!(complete.input[0].witness.to_vec(), vec![vec![0; 32]]);
}

#[test]
fn reviewer_negotiated_sizes_and_sigops() {
    let outputs = serialize(&vec![TxOut {
        value: Amount::ZERO,
        script_pubkey: ScriptBuf::from_bytes(vec![0x51]),
    }]);
    for total in [12, 16, 32] {
        let job = Job::prepare(template(vec![3, 0x40, 0x0d, 3]), &outputs,
            vec![1, 2, 3, 4], total - 4, 1, 1, [255;32]).unwrap();
        let complete = job.complete_coinbase(&vec![0xae;total - 4]).unwrap();
        assert_eq!(complete.input[0].script_sig.as_bytes()[4], total as u8);
        assert_eq!(complete.input[0].script_sig.count_sigops_legacy(), 0);
    }
    let mut raw = vec![3, 0x40, 0x0d, 3];
    raw.extend_from_slice(&[0xae;12]);
    assert_eq!(ScriptBuf::from_bytes(raw).count_sigops_legacy(), 240);
}
#[test]
fn fixed_pool_and_miner_tags_survive_extranonce_and_script_is_bounded() {
    let mut prefix = vec![3, 0x40, 0x0d, 3, 10];
    prefix.extend_from_slice(b"/TestPool/");
    prefix.push(7);
    prefix.extend_from_slice(b"/miner/");
    let job = prepare(prefix.clone()).unwrap();
    let complete = job.complete_coinbase(&[0x4c; 8]).unwrap();
    assert_eq!(
        &complete.input[0].script_sig.as_bytes()[..prefix.len()],
        prefix
    );
    assert!(prepare(vec![0; 88]).is_err());
}

#[test]
fn configured_tags_are_encoded_as_independent_script_pushes() {
    let mut value = template(vec![3, 0x40, 0x0d, 3]);
    value.add_coinbase_tags("/TestPool/", "/miner/").unwrap();
    assert_eq!(
        hex::decode(&value.coinbase_prefix).unwrap(),
        b"\x03\x40\x0d\x03\x0a/TestPool/\x07/miner/"
    );
    assert!(value.add_coinbase_tags(&"x".repeat(76), "").is_err());
}


#[test]
fn maximum_script_size_is_enforced() {
    let outputs=serialize(&vec![TxOut{value:Amount::ZERO,script_pubkey:ScriptBuf::new()}]);
    for (length,valid) in [(67,true),(68,false)] {
        let job=Job::prepare(template(vec![0;length]), &outputs,vec![1,2,3,4],28,1,1,[255;32]);
        assert_eq!(job.is_ok(),valid);
        if let Ok(job)=job {assert_eq!(job.coinbase.input[0].script_sig.len(),100);}
    }
}
