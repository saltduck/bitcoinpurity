# TASK-032: Rust protocol gateway and lifecycle

Implement pinned SRI integration, authenticated Full-Template declaration, V1 conversion, bounded state and same-pool retry through C ABI.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Implemented and exercised by independent local pool/JDS acceptance on macOS and Linux. Platform gates remain in TASK-035.

## Authenticated transport checkpoint

Pinned SRI commit d1d30f3e755b0a5108633e1be7012034a8caaecd and Rust 1.96.0,
with a committed Cargo lockfile. Added cancellation-safe framed Noise transport,
bounded connection/write timeouts, and support for borrowed/owned SRI messages.
The independent client/server test verifies encrypted messages and rejection of
a different authority key. It first failed against the missing wire module.

## Gateway runtime checkpoint

Implemented a node-owned Rust runtime, bounded C ABI, Full-Template JDS flow,
exact missing-transaction response, custom-job acknowledgement gate, SRI channel
share validation, V1 translation and miner authorization/version rolling.
Share acknowledgement maps are bounded and connection-local. Remote failures
close miners and retry the original authenticated pool; no alternate mining
mode exists. The unavailable-pool regression first failed against the missing
runtime, then verified no downstream work and prompt joined shutdown.

The local C++/Rust gateway has passed actual V1 share and block PoW against an
independent SRI-server fixture, including block propagation to a second node.
Detailed fixture acceptance is tracked in TASK-033; platform/Qt checks remain.


## Protocol boundary follow-up

Production transport validates framing, extension and channel flags using the
pinned SRI codec. Duplicate or excessive JDS transaction indices are rejected
before materializing transaction bytes, preventing response amplification.
An adversarial independent fixture first demonstrated a duplicate response,
then verified that the connection closes without producing one.

Local credential failures now return false while retaining the original
authorization deadline; the earlier writer race closed the socket before the
queued false result was sent. The external miner test first observed EOF and
then verified the actual false response after the fix. Block result/hash/time
and fractional target difficulty are included in the shared status snapshot.


Negotiation includes the configured endpoint host and port (IPv6 brackets are
removed from the host field). The independent fixture validates these metadata
fields; it first rejected the former empty-host/zero-port messages.
