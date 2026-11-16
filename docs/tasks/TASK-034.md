# TASK-034: Headers presync ASERT difficulty check (M-1)

- Status: Implemented

Implements the [requirements](../../input/requirements.md) section "Headers
presync ASERT difficulty check" per the
[architecture](../architecture/validation-issues.md). Numbered 034 because
TASK-030 to TASK-033 are used on master or other branches.

## Verification

`headers_sync_chainwork_tests` with mainnet parameters, written first and
failing on the previous sources:

- a post-activation `powLimit` header is rejected in presync, starting above
  and below the ASERT anchor;
- the exact ASERT header is accepted, including the harder activation block;
- a header more than 2 hours in the future is rejected;
- redownload rejects a wrong-`nBits` header that presync never saw.

Fuzz target `headers_sync_state_asert` starts from a synthetic mainnet index
below the anchor, between anchor and activation, or after activation. It can
give fuzzed headers the required nBits, and asserts that every header presync
accepts or redownload releases has the nBits and time full validation requires.
It crashes on the previous sources.
