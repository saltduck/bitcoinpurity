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
