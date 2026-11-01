# Embedded SV2 configuration and status contract

Implementation status: the daemon, Rust runtime, build option and RPC are\nimplemented and locally tested. Qt and cross-platform acceptance are pending.

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
upstream identity. Qt consumes the same typed snapshot. Accepted means upstream
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
