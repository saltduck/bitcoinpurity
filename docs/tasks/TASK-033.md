# TASK-033: Independent pool and V1 end-to-end acceptance

Test-only independent pool/JDS, actual V1 PoW, acknowledged shares and block propagation to a second regtest node.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Implemented; local macOS and Linux end-to-end scenarios pass. Native Windows gates are tracked in TASK-035.

## Independent regtest acceptance

Implemented a feature-gated test pool/JDS binary using SRI server-side channel
validation, separate from the gateway's client-side channel. JDS requests and
verifies full witness transactions by wtxid before approving tokens. The pool
independently validates share PoW and reconstructs complete block candidates.

feature_sv2.py computes actual V1 PoW outside the node and verifies accepted
non-block shares, duplicate rejection, a network-target block, local acceptance,
P2P propagation and identical reconstruction by the pool. It also verifies
pending shares are never prematurely credited, upstream loss closes miners,
same-pool recovery, declaration rejection, wrong authority and bounded shutdown.
The complete scenario passed repeatedly on macOS arm64. An initial fixture
target mismatch was caught by independent server share validation and corrected.
The fixture is test-only and does not implement production pool operation.


## Reorg and rolling-version acceptance

The external miner negotiates the BIP310 mask and solves a network-target block
with a rolled version. Node and independent pool reconstruct the same header.
A longer competing branch replaces the tip; new work references that local
parent and old V1 job IDs cannot earn credit. The RPC regression first exposed
missing fractional share-difficulty metadata; it now comes from the same
acknowledged job as the miner's target.


Additional fault acceptance covers wrong local credentials, unique miner
extranonces, a pool target that rejects locally computed high hashes, protocol
version/feature mismatch, silent JDS timeout and duplicate missing-transaction
indices with two real mempool transactions. Last local block hash/result/time
are checked against the actual accepted block.
