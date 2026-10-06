# TASK-014: Validation and signed-manifest corrections

Status: initial implementation locally verified; the subsequent mainnet
correction-height decision is tracked in [TASK-017](TASK-017.md).

Scope: deep-reorg parking provenance/RPC, permanent RDTS grandfather boundary
with historical compatibility gating, signed manifest duplicate-key rejection
and release artifact URI/redirect restrictions. Replay protection is excluded.

Acceptance: old-code failures demonstrated; defaults and threshold equality
retained; missing ancestor/null pblock blocked; unpark activates; fixed RDTS
boundary stable across BIP9 transitions in regtest; generic BIP9 unchanged;
ambiguous signed DOMs never consumed; documentation and exact test results
recorded in `doc/validation-issues.md`.

This initial patch left the mainnet correction gate unset. TASK-017 subsequently
sets it to 961637. Historical compatibility and production
rollout are separate from that code decision.

The acceptance-time parking architecture in [TASK-015](TASK-015.md) supersedes
this task's parking activation override; TASK-017 supersedes its unset mainnet
RDTS gate. Manifest work is unchanged.
