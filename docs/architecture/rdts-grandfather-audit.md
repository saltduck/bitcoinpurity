# RDTS grandfather audit architecture

Baseline: `v1.0.0`, commit `09a69c0e560f5dc6cbc5167b69a0eaf8d38d0fe1`.
The original `ConnectBlock` dynamic boundary and per-input flags are unchanged.
Only existing `GetBlockScriptFlags` linkage is exposed for diagnostics; its body
and all production callers are unchanged.

The RPC snapshots active-chain index pointers under `cs_main`, including the
tip. BlockManager's `BlockMap` only inserts entries and never erases them during
operation; unordered_map rehash preserves pointers. Hash, height and ancestry
are immutable. RPC threads stop before ChainstateManager is destroyed. Pruning
can remove file availability, which is checked by existing read methods under
short internal locks; disk I/O occurs after releasing them. Reorgs do not change
the captured ancestry. Completion compares the active tip against the snapshot.

`ReadBlock` verifies the expected hash and `ReadBlockUndo` verifies the checksum
bound to the previous hash. Non-coinbase transaction i maps to undo i-1; vin j
maps to historical Coin j. Entire vector relationships and Coin values/heights
are checked before script work. Same-block spends are valid. Genesis has no undo
and is explicitly incomplete when requested. Missing/corrupt data is counted,
logged with height/hash, and does not turn into a successful audit.

Normal flags come from the unmodified production `GetBlockScriptFlags`. A local
VersionBitsCache executes the existing `DeploymentActiveAt` and
`StateSinceHeight(index.pprev, consensus, DEPLOYMENT_REDUCED_DATA)` implementations
for the old boundary when active. Old input flags remove the complete RDTS mask
only when Coin.nHeight is below that boundary. Candidates require Coin.nHeight
at or above the fixed boundary and old flags missing any mandatory RDTS bit.
The mainnet fixed boundary comes from consensus (961637); no BIP9 period height
is hardcoded.

Transactions containing candidates build local PrecomputedTransactionData with
ALL spent outputs from undo. Each candidate executes two direct CScriptChecks
on the same vin, spent output and precomputation: old flags and old OR the full
RDTS mask. Other inputs are never made strict by that vin's check. Direct checks
bypass the full script execution cache. `cacheStore=false` can erase shared
signature-cache hits, so this RPC owns an empty temporary SignatureCache and
never references the node's validation caches. The native interpreter covers
element limits (legacy, P2SH, P2WSH, Tapscript), annex, control depth, executed
OP_IF/OP_NOTIF, all OP_SUCCESSx, unknown witness programs and unknown leaves.
Output rules are not audited.

No chainstate or UTXO view is acquired, no validation/migration/connect/disconnect
operation is invoked, and no index status or database is written. Only diagnostic
logs and local audit state are produced. Reads can race with pruning and then
report missing data. Logs naturally append to debug.log; the guarantee concerns
consensus data, databases and block/undo storage, not diagnostic log bytes.

Memory is O(snapshot range + one block/undo + at most max_failures details).
Progress logs every 100 attempted blocks. Shutdown/RPC interruption is polled
between blocks, transactions and candidate inputs, returns incomplete, and logs
a summary. A single disk read or native script check finishes before the next
interruption poll.
