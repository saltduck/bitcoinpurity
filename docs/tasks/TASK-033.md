# TASK-033: DATUM public exposure and payout script hardening (M-3, M-4)

- Status: Implemented
- Depends on: TASK-003, TASK-007, TASK-009

Implements the DATUM security criteria in [requirements](../../input/requirements.md)
added for audit findings M-3 and M-4. Numbered 033 because TASK-030/031/032 are
used on other branches.

## Contract

- `datumupnp=1` requires `datumauth=1` (startup error; Qt Settings refuses to save).
- With `datumauth=0`, Stratum accepts only loopback, private and link-local
  sources, including IPv4-mapped IPv6 forms.
- The authentication limiter keys IPv6 by /64, holds 1024 sources, never evicts
  a source in active cooldown, and refuses new sources while full of cooldowns.
- `datumaddress` and the Qt hot-reload path reject `WitnessUnknown`
  destinations and output scripts longer than 34 bytes.

## Verification

`feature_datum.py` covers UPnP without authentication, a 42-byte witness v2
address and a short witness v2 address, written before the implementation and
failing on the previous sources. Source-address filtering is verified manually
with a non-private address assigned to the loopback interface; the limiter
eviction policy is reviewed in code because exercising it needs more than 1024
failing sources.
