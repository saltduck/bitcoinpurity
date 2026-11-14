# TASK-034: Configuration, RPC and Qt integration

Mutually exclusive modes, safe persistence and shared RPC/Qt status; test outages, reorg and restart.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Implemented; daemon and Qt verified locally on macOS and Linux. Native Windows CI remains tracked in TASK-049.

## Daemon configuration and RPC checkpoint

Added opt-in sv2 configuration, authenticated endpoint/key validation, sensitive
argument marking, and mutual exclusion with DATUM before subsystem validation.
Invalid local configuration fails startup. The miner listener is bound before
startup succeeds; remote failures only affect the gateway. Start/interrupt/join
are ordered around node lifetime. getsv2info returns the shared aggregate runtime
snapshot without worker names, local credentials or the upstream account.

The daemon is linked against the Rust static library and has passed the complete
local V1/SV2/JDS proof-of-work functional scenario. Qt integration is implemented below.


## Qt mode and status checkpoint

Added restart-based Disabled/DATUM Solo/SV2 selection, endpoint/authority/account
settings and masked local credentials. Only edited values are written to the
read/write configuration file; settings.json does not receive passwords.
Validation rejects characters truncated by the config parser. Tests first
caught unnecessary writes from an unchanged dialog and acceptance of # in an
edited password, then passed after the fixes.

Mining pages use the getsv2info snapshot, show local-valid/pending/abandoned and
pool-acknowledged counters, and retain fractional work for regtest hashrate.
The fractional-work regression first failed under the old integer accumulator.
The macOS Qt application and full macOS/Linux Qt suites pass. macOS's existing
minimal-platform IntroTests skip is unchanged.
