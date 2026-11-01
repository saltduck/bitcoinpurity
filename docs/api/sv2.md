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
acknowledgement. Existing RPC and Cap'n Proto field numbers retain their contracts.

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

Optional IPC restoration (TASK-036) preserves this gateway API and existing
Cap'n Proto ordinals; it does not move the gateway to a subprocess.
