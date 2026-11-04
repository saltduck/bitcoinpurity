# TASK-032: Signed chain decisions for parked splits (proposed)

Status: requirements only; implementation not started. Numbered 032 because
TASK-030/031 are used by the catch-up parking branch.

Implement the [requirements](../../input/requirements.md) section "Signed chain
decisions for parked splits" per the [architecture](../architecture/chain-decisions.md)
and [contract](../api/chain-decisions.md).

## Before implementation

Settle the open decisions: threshold and key holders, default mode (and any
`apply` default for miners), rewind limit, and whether HTTPS fetch ships with
the manual phase.

## Acceptance (phase 1)

- Valid threshold decisions resolve a parked split: the chosen chain becomes
  active, conflicting branches are parked and the warning clears.
- Rejected: wrong domain/network, below threshold, duplicate signer keys,
  replayed or lower sequence, expired, unknown or duplicate JSON keys.
- An invalid chosen block is never accepted; the decision is rejected with a
  warning. Targets at or below activation height and rewinds above the limit
  are not applied automatically.
- Missing blocks leave the decision pending until fetched and validated.
- `confirm` mode only warns until `applychaindecision`; `off` ignores.
- A newer decision reverses an earlier one. The last applied sequence survives
  restart.
- Tests are written first and fail on the current sources.
