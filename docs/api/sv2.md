# Embedded SV2 configuration and status contract

Implementation status: this is the target contract. The SV2 runtime, build
option, configuration options, RPC and Qt integration are not implemented yet.

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
