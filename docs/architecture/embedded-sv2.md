# Embedded Stratum V2 architecture

V1 ASIC -> in-process Rust gateway -> authenticated SV2 Mining + JDS endpoints.
C++ owns immutable local templates, transaction bytes and normal block validation.
Rust owns Noise, framing, channels, V1 translation and bounded job/share state.
C ABI handles expose no C++ objects; buffers have explicit ownership and lengths.
Exceptions and panics cannot cross the ABI. Cancel before join; finish callbacks
before provider destruction. No public template distribution listener is added.

Use unmodified SRI stratum commit d1d30f3e755b0a5108633e1be7012034a8caaecd.
Implement application policy locally: upstream applications automatically fall
back to Solo, violating this feature's required failure behavior.

## Core backport assessment

| PR | Application |
| --- | --- |
| #33819 | Structured coinbase information; raw getters remain in-process only, with Core v32.1 IPC fields. |
| #34568 | Adopt Core v32.1 IPC method ordinals and context layout under TASK-038; audit blocking-call execution context. |
| #34184 | Interruptible initial sync/catch-up gating for SV2, preserving internal caller timing. |
| #34644 | Normal block submission with rejection details. |
| #34672 | Shared submission handling; distinguish accepted, duplicate, invalid and inconclusive. |
| #34020 | Batch mempool txid/wtxid lookup; retain exact template transactions after eviction. |
| #33745 | Reset witness, merkle and full-block validation caches when reconstructing solutions. |

## Data flow

Negotiate Mining/JDS, allocate token, reserve coinbase resources, obtain local
template, declare transactions, provide missing bytes, obtain custom-job success,
and only then publish V1 work. Keep template, connection generation, upstream
channel/job and V1 job IDs distinct. Do not replay shares across connections.
Separate local validation from upstream credit. Preserve local version, ASERT
nBits, parent, height and selection; rolling masks exclude mandatory bits.
Validate assembled coinbase outputs, witness and resources before issuing work.
Submit network-target solutions locally and upstream independently.

## Lifecycle

Invalid local configuration fails startup. Remote outages leave the node running
but close miners while reconnecting with bounded backoff. No Solo, backup pool
or pool-template fallback. DATUM and SV2 are mutually exclusive. Start after
chainstate readiness; cancel listeners/waits and join workers before destroying
node dependencies. Mainnet and regtest only.


## Runtime bounds and integration

The provider retains at most 32 templates; Rust keeps 16 job mappings and 1024
pending shares. Miners have bounded writers, 16 KiB lines, authorization
deadlines and rate limits. Connection/handshake/write/job-ack timeouts and retry
backoff capped at 30 seconds bound waits. Lost pending shares become abandoned.

One owned thread hosts a current-thread Tokio runtime. C++ keeps its provider
alive until Rust cancellation and join finish. No usable work exists without
an upstream-acknowledged job. RPC and Qt read the same aggregate snapshot;
settings require restart. Rust 1.96.0, SRI and lockfiles are pinned; dependency
sources are unmodified.

## Optional IPC restoration

TASK-036 restored the separate multi-process build. TASK-038 upgrades its
libmultiprocess dependency to the unmodified Core v32.1 snapshot and replaces the
inherited wire layout with Core v32.1. SV2 retains its same-process C ABI bridge;
Purity-specific convenience methods are not additional IPC methods.


## Release acceptance

Native platform jobs exercise both build switches. Release packaging must also
validate the extracted daemon/GUI and macOS application bundle, not just compile
the source tree. Manual release runs do not publish tags or GitHub Releases.

Pre-release review follow-up: [TASK-037](../tasks/TASK-037.md) tracks F1–F4 correctness fixes.
PR #44 remains draft. Existing local fixture results do not establish independent
CKPool interoperability or native Windows acceptance.

The 2026-11-02 decision supersedes preservation of the inherited IPC wire layout:
Core v32.1 is the single target contract (TASK-038). Existing IPC clients must
regenerate bindings and update calls; no parallel legacy socket is planned.
Purity consensus and in-process SV2 behavior remain project-owned. CKPool's
v31-era binding must be updated externally; its current layout is not the target.

### Generic SV2 interoperability corrections (2026-11-02)

- Implement TASK-039 through TASK-046 as separate commits, with failing tests
  before each correction. Retain existing F1-F4 and Core v32.1 IPC behavior.
- Separate production/test Cargo outputs; expose bounded, sanitized stage/error
  diagnostics through the shared RPC/Qt snapshot; retain failures until recovery.
- Reset reconnect backoff after a session has ever obtained confirmed custom work.
- Build BIP34 + optional pool/miner pushes + negotiated extranonce data push.
  Tags default empty; UTF-8 encoded tags including push bytes total at most 59.
  Validate actual scriptSig <=100 bytes. Preserve witness, payouts and templates.
- Add sv2pooltag/sv2minertag to CLI/configuration/Qt, effective after restart.
- Unavailable subscribe receives error 20, flushed before close. Preserve client
  limits, deadlines and disconnect of subscribed miners when work disappears.
- Retain generic empty-tag and 12/16/32-byte cases. Do not implement fixed PUSH12
  or a named-pool mode. Independent pool/native Windows acceptance remains open.
