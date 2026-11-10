# Embedded SV2 configuration and status contract

Implementation status: daemon, Rust runtime, C ABI, RPC and Qt are implemented.
The complete local chain passes on macOS and Linux. Native Windows/Qt CI and
production pool interoperability remain separate acceptance gates.

Runtime is opt-in with sv2=1; datum=1 and sv2=1 together are an error.
Mainnet and regtest only. BUILD_SV2 defaults to ON; OFF excludes Rust dependencies.
V1 defaults: 127.0.0.1:23334, 32 clients, 4 per IP. Configure optional local
authentication, Mining/JDS endpoints and authority public keys, and upstream
identity. One pool pair is supported. Changes require restart; normal argument
precedence applies. Passwords are excluded from settings.json, status and logs.

getsv2info is read-only. Snapshot states: disabled, connecting, declaring,
running, retrying and stopped. Report template state, client counts, local-valid,
upstream-accepted, rejected and pending shares, local block submission results
and sanitized errors. Exclude worker names, miner addresses, credentials and
upstream identity. Qt consumes the same runtime snapshot. Accepted means upstream
acknowledgement. The gateway RPC retains its contract. Mining IPC migrates to Core v32.1 under TASK-038; older IPC clients must update.

## Daemon options

| Option | Default / meaning |
| --- | --- |
| sv2 | 0; enable pooled gateway |
| sv2listen / sv2port | 127.0.0.1 / 23334; numeric V1 listener |
| sv2pool / sv2jds | Required host:port endpoints |
| sv2poolkey / sv2jdskey | Required 32-byte hex or SRI Base58 authority public keys |
| sv2identity | Required upstream account; excluded from status/log arguments |
| sv2auth | 0; require local credentials when enabled |
| sv2user / sv2password | Local username and password; user.worker is accepted |
| sv2maxclients / sv2maxperip | 32 / 4; bounded to 4096 total |

The runtime uses one extended channel and one upstream identity for the gateway.
Worker identities remain local. Shares below the negotiated upstream target are
rejected locally. Only upstream acknowledgement increases accepted_shares and
accepted_difficulty. abandoned_shares counts pending submissions whose connection
was lost without an acknowledgement; they are never credited as accepted.

Rust 1.96.0 and Cargo.lock are required for BUILD_SV2=ON. Cross builds specify
SV2_RUST_TARGET. BUILD_SV2=OFF does not discover or invoke Rust.

depends and Guix release builds build SV2 offline. depends fetches the official
Rust 1.96.0 rustc, cargo and per-target rust-std archives (pinned by the sha256
published on static.rust-lang.org) and every crate listed in Cargo.lock (pinned
by its Cargo.lock checksum), plus the stratum source archive. The generated
toolchain file then sets BUILD_SV2, SV2_CARGO, SV2_RUSTC, SV2_RUST_TARGET and
SV2_VENDOR_DIR. With SV2_VENDOR_DIR set, CMake builds a copy of the crate with
cargo --offline and fails if any locked crate version differs from Cargo.lock.
Pass NO_SV2=1 to depends to omit it. After changing Cargo.lock, regenerate
depends/packages/sv2_crates.mk with contrib/devtools/gen-sv2-crates-mk.py.


## Status snapshot

Connection fields are enabled, running, status and error. Local listener and
aggregate clients/subscribed_clients/authorized_clients describe the V1 side.
Job fields include current_height, previous_block_hash, nbits, job_id,
job_created, network_difficulty, share_difficulty, transactions, template_size,
template_weight and coinbase_value. Times are Unix seconds and difficulties
are floating-point values; small regtest difficulties must not be truncated.

Share fields are local_valid_shares, accepted_shares, rejected_shares,
pending_shares, abandoned_shares, accepted_difficulty, best_share_difficulty and
last_share_time. Block fields are block_candidates, blocks_accepted and
blocks_rejected, last_block_result, last_block_hash and last_block_time.
Accepted work and best-share statistics require a pool ACK.
The disabled snapshot contains only enabled=false, running=false and
status=disabled. No per-worker data, pool account or credentials are returned.

Qt presents Disabled, DATUM Solo and SV2 pool modes (DATUM is omitted when not
compiled). It reads this same running snapshot, so pending restart settings do
not change the displayed active backend. Credentials use a masked field and
the read/write configuration file; unchanged command-line values are not
silently persisted. Edited values containing a configuration comment character
(#), newlines or surrounding whitespace cannot be saved. Normal command-line
and explicit configuration-file precedence remains in effect.

## Local acceptance

Configure with BUILD_TESTS=ON, build bitcoind, bitcoin-cli and sv2_test_pool,
then run:

~~~sh
python3 test/functional/feature_sv2.py --configfile=/path/to/build/test/config.ini
~~~

The test-only fixture is not installed or included in release packages.
Production pools must implement this pinned Full-Template protocol dialect;
local success does not establish interoperability with every SV2 pool.

IPC restoration (TASK-036) preserves this gateway API. TASK-038 subsequently
replaces the inherited IPC wire layout with Core v32.1; the gateway stays in process.


TASK-035 release acceptance exercises the same RPC contract from packaged
executables; packaging changes introduce no RPC or protocol migration.

Pre-release review follow-up: [TASK-037](../tasks/TASK-037.md) tracks F1–F4 correctness fixes.
PR #44 remains draft. Existing local fixture results do not establish independent
CKPool interoperability or native Windows acceptance.

Share counting clarification: well-formed submissions rejected for unknown work,
queue capacity, pending capacity or validation failure increment rejected_shares
once. Upstream explicit rejection also increments it once. Unacknowledged
attempted upstream submissions become abandoned on session loss; accepted counts
require ACK. Malformed JSON, invalid field encoding and unauthenticated requests
are protocol errors, excluded from these share counters.

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

`getsv2info.stage` identifies the current connection/authentication, Setup,
channel, allocation, template, declaration, custom ACK or mining operation.
`error` retains the failed stage and bounded sanitized code across retries until
confirmed work recovers. Server error_details and account credentials are omitted.

`sv2pooltag` and `sv2minertag` default to empty. Their nonempty UTF-8 data pushes
total at most 59 bytes including length bytes; control characters are rejected.
They precede a negotiated-length extranonce data push. CLI/configuration/Qt use
the same validation and changes require restart. An unavailable V1 subscribe
returns error 20 with result=null, then EOF after the response is flushed.

### Periodic SV2 job refresh (2026-11-03)

`sv2jobinterval` is available through CLI, configuration files and Qt. It defaults
to 30 seconds and accepts integer seconds from 30 through 60 inclusive; invalid
values fail startup when SV2 is enabled. Changes require restart and retain
normal command-line/configuration precedence. Ordinary mempool changes are
coalesced until the next refresh deadline, without postponing that deadline.
Even an unchanged mempool gets periodic fresh work. New chain tips/reorgs and
invalid work are handled immediately; upstream target changes retain their
existing immediate job installation and per-job difficulty rules. Work is only
published after a valid Custom Job acknowledgement. Same-parent refreshes retain
valid past work and use clean_jobs=false; parent changes invalidate old work.
The monotonic template refresh interval is not a hard delivery guarantee during
upstream latency or failure. No miner-model detection or pool-specific behavior
is introduced. See TASK-047 for regression evidence.
