# TASK-035: Catch-up-aware deep-reorg parking (audit H-1)

Implement the [requirements](../../input/requirements.md) section
"Catch-up-aware deep-reorg parking" and its
[architecture](../architecture/validation-issues.md) and
[RPC contract](../api/validation-issues.md).

## Acceptance

- A lagging node that first receives a short sibling extension and then the
  longer honest chain ends on the honest chain without parking.
- After catch-up ends, a competing chain rewinding more than 3 protected
  blocks is parked and a node warning is raised; `unparkblock` clears it.
- Blocks protected before a restart keep the competing chain parked while the
  restarted node is still catching up.
- Catch-up does not end with fewer agreeing qualifying peers than
  `-parkcatchuppeers`; `getblockchaininfo.parkcatchup` reports the state.
- Existing parking boundary, out-of-order, restart and disabled-policy tests
  keep passing. Consensus, P2P messages and disk formats are unchanged.

## Validation

Validated on Linux x86_64 (GCC, RelWithDebInfo, DATUM and SQLite wallet
enabled, GUI disabled) on 2026-11-04.

- Test-first: the lagging-node scenario of `feature_park_catchup.py` failed on
  the original sources (the honest chain was parked with `rewind=4 exceeds
  parkreorgdepth=3` and the node stayed on the short branch).
- `feature_park_catchup.py`, `feature_park_deep_reorg.py`, all
  `feature_parked_chain.py` scenarios and the new
  `validation_chainstate_tests/deep_reorg_catchup_blocks_unprotected` pass.
  Existing parking unit tests treat their setup chain as history accepted
  after catch-up.
- `p2p_outbound_eviction.py` initially regressed because catch-up queries took
  the per-peer getheaders slot; peers with a running chain-sync timer are now
  skipped and the test passes.
- Full suites: 876 unit cases pass; 303 functional tests pass, 65 skipped.
  `rpc_bind.py --ipv4`, `rpc_bind.py --nonloopback` and `feature_bind_extra.py`
  fail identically on unmodified sources (no `/proc/net/tcp6` in the sandbox).
- GUI warning display was not runtime-tested (GUI not built).
