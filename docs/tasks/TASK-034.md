# TASK-034: Configuration, RPC and Qt integration

Mutually exclusive modes, safe persistence and shared RPC/Qt status; test outages, reorg and restart.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Pending implementation and validation.

## Daemon configuration and RPC checkpoint

Added opt-in sv2 configuration, authenticated endpoint/key validation, sensitive
argument marking, and mutual exclusion with DATUM before subsystem validation.
Invalid local configuration fails startup. The miner listener is bound before
startup succeeds; remote failures only affect the gateway. Start/interrupt/join
are ordered around node lifetime. getsv2info returns the shared aggregate runtime
snapshot without worker names, local credentials or the upstream account.

The daemon is linked against the Rust static library and has passed the complete
local V1/SV2/JDS proof-of-work functional scenario. Qt integration remains pending.
