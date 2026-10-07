# auditrdtsgrandfather (debug/diagnostic RPC)

`auditrdtsgrandfather ( start_height end_height max_failures )`

- start_height: integer >=0, defaults to consensus.nPurityActivationHeight.
- end_height: integer >=start_height and <=snapshot tip, defaults to tip.
- max_failures: integer 0-10000, defaults to 1000. Totals are uncapped.
- Invalid ranges/caps return RPC_INVALID_PARAMETER (-8). On networks whose
  Purity activation exceeds the tip, supply an explicit valid range.

Results include start/end, fixed_boundary, snapshot_tip_height/hash,
blocks_scanned (readable/consistent block+undo pairs), transactions_scanned
(includes coinbases), inputs_scanned (excludes coinbase inputs), candidate_inputs,
old_pass_strict_pass, old_pass_strict_fail, old_fail_strict_fail,
old_fail_strict_pass, missing_block_data, missing_undo_data, invalid_undo_data,
complete, interrupted, chain_changed_during_scan, failures_returned,
failures_total, failures_truncated, and failures.

Each failure has classification, block_height/hash, txid, vin, prevout_txid/vout,
prevout_height, old_boundary, fixed_boundary, old_flags, strict_flags,
old_script_error/string and strict_script_error/string. ScriptError values are
native numeric enums and descriptions come from ScriptErrorString.
OLD_PASS_STRICT_FAIL is the compatibility danger; old-check failures are
anomalies and are included. Counters continue after script failures and capped
details. Missing data is logged per height/hash and causes incomplete results.
Completeness concerns data/execution, not compatibility: inspect outcome counts.
A changed tip describes a valid earlier snapshot; it does not mean incomplete.

```sh
bitcoin-cli -rpcclienttimeout=0 auditrdtsgrandfather 961637 <current_tip>
```

Run against an audit-enabled v1.0.0 archive/full node using existing blk/rev data.
No reindex or database rebuild. Deploying a newly built RPC-enabled binary follows
normal node restart procedures; once loaded, invoking the RPC needs no restart.
No mainnet compatibility claim is made before an actual complete historical audit.
