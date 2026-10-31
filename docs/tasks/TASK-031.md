# TASK-031: Mining template and validation bridge

Test first: coinbase fields, resources, interruption, witness cache reset, submission results and batch transaction lookup.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Pending implementation and validation.

## Witness validation cache backport (#33745)

The regression test first failed on the original implementation: a replacement
coinbase without witness was accepted and advanced the tip. Clearing all three
block validation caches after reconstruction makes the regression pass on
macOS arm64 (Release, wallet/GUI/DATUM disabled). This subtask does not complete
the template bridge or any gateway runtime acceptance.

## Structured coinbase and submission results (#33819/#34644/#34672)

Added an in-process structured coinbase accessor while retaining all existing
raw getters and IPC field numbers. Added a normal-validation submission helper
with reason/debug, duplicate and inconclusive handling. The legacy boolean
submission method retains its existing contract. New accessor/submission tests
first failed to compile against the original API, then passed after implementation.
The complete miner_tests suite passed on macOS arm64; no other platform or IPC
build has been verified yet. This is a partial adaptation, not the full Core IPC
backport or a completed SV2 gateway.

## Batch mempool transaction lookup (#34020)

Added in-process Mining lookup methods for txid and witness ID, under one
mempool snapshot lock, retaining request order, duplicate entries and null
missing entries. Tests cover distinct txid/wtxid, full witness retention,
missing IDs and empty batches. The new API first failed to compile, then
all six miner_tests passed on macOS arm64. These methods are not exported over
Cap'n Proto; existing IPC schemas remain unchanged.

## In-process full-block submission

Mining::submitBlock now exposes the detailed submission helper to the future
gateway without changing the old template submission signature or IPC schema.
It clears cached validation on a private block copy, so even an in-process
caller cannot reuse stale checks to omit coinbase witness validation.

## Per-client interruption (#34184, partial)

Added permanent interruption of a dedicated in-process Mining handle. Both
initial-tip and subsequent-tip waits wake and return no result; other handles
remain operational. New regression first failed to compile for missing interrupt,
then all seven miner_tests passed. Mainnet IBD/header cooldown, strict resource
reservation and the C ABI template bridge remain pending.
