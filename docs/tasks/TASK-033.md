# TASK-033: Independent pool and V1 end-to-end acceptance

Test-only independent pool/JDS, actual V1 PoW, acknowledged shares and block propagation to a second regtest node.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Implemented; local macOS end-to-end scenario passes. Other platform gates are tracked in TASK-035.

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
