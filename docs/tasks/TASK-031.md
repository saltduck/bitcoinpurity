# TASK-031: Mining template and validation bridge

Test first: coinbase fields, resources, interruption, witness cache reset, submission results and batch transaction lookup.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Template provider and C ABI callbacks implemented and locally validated; full gateway integration is tracked in TASK-032 onward.

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

## Validation checkpoint

Local macOS arm64, AppleClang 21, Release, WERROR=OFF, wallet and GUI disabled:

- Built test_bitcoin, bitcoind and bitcoin-cli with DATUM OFF and ON.
- All seven miner_tests passed with DATUM OFF and ON.
- mining_basic.py, mining_getblocktemplate_longpoll.py and feature_datum.py passed.
- Documentation links and git diff --check passed.

Existing mismatched bilingual_str declaration and duplicate-link-library warnings
remain. Linux, Windows, Qt, IPC, sanitizers and fuzzing were not validated.
No SV2 runtime or end-to-end mining has been implemented or tested; TASK-031
remains partial and TASK-032 through TASK-035 remain pending.

## Template provider and C ABI implementation

Added a node-owned provider with bounded immutable template retention, exact
witness transaction snapshots, wtxid/Merkle metadata and stable cached IDs.
Pool output constraints reserve both byte size and weight plus sigops; requests
that exceed configured limits fail instead of being clamped. Mainnet gates
initial sync and header catch-up. Complete coinbases are validated before work
is declared usable; solutions use normal full block submission. A per-provider
interrupt and explicitly allocated/released length-delimited C buffers form the
Rust boundary. Concurrent callbacks serialize on the provider, and interruption
does not require waiting for that mutex.

Eight miner_tests pass, including callback roundtrip/release, malformed output
data, excessive sigops, complete witness validation, stable IDs and interruption.
The new bridge test first failed to build against the missing provider.
The provider has not yet been exercised by the full Rust gateway or on other
platforms; those acceptance gates remain open.
