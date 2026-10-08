# CoinStatsIndex overflow backport and verification

Implemented on `codex/backport-core-30469`, based on master
`b9a1bfddcae274e02572940c1f6c855f0185a635`.

## Change and upstream comparison

Reference: [Bitcoin Core #30469](https://github.com/bitcoin/bitcoin/pull/30469),
merge `1861030bea7f55d08173c34d6fa11a16e6eff454`.
Purity's original coinstatsindex.cpp matches Core v29.4 byte for byte. The
backport's DBVal definition matches the merged format present in Core v30.0.

- Three cumulative flow counters are arith_uint256 in index state, DBVal and
  CCoinsStats. Reward arithmetic uses wide intermediates; the bounded result
  is checked before conversion to CAmount. Supply-bounded values retain CAmount.
- Redundant unspendable totals are removed and derived from their constituents.
- New records use the upstream field order and ArithToUint256/UintToArith256
  conversion. DBVal grows from 128 to 192 bytes (a height record is 224 bytes).
- The new index lives in `indexes/coinstatsindex/db/`; legacy
  `indexes/coinstats/` is warned about and preserved without being opened.
- Append verifies m_current_block_hash before changing counters. Startup and
  rewind restore that hash. Rewind reverses MuHash, skips historical BIP30
  coinbases, verifies the prior digest and restores every cumulative counter.
- Hash-index predecessor lookup reads DBVal, not a height-record pair. The
  overflow test forces this fallback during both reload and rewind.

Adaptations from upstream:

- Retain existing CustomRewind, ReverseBlock(CBlock, CBlockIndex*), local
  ReadBlockUndo, interfaces::Chain and IsBIP30Unspendable(*pindex). Do not import
  [#32694](https://github.com/bitcoin/bitcoin/pull/32694), CustomRemove,
  notification/undo-data interfaces, or unrelated fuzz cleanup.
- Warning text describes pre-backport Purity, without Core's release-number
  assumptions or future automatic-delete TODO.
- Check each wide per-block RPC difference against INT64_MAX before GetLow64.
  Ordinary field names and amounts remain identical. Extreme values return
  RPC_INTERNAL_ERROR (-32603), not truncation or wraparound.

## Upgrade and database compatibility

Old and new CoinStatsIndex formats are incompatible and use separate
locations. No in-place migration, legacy deletion/rename or automatic global
reindex is introduced. A real old Purity database was generated with the
unmodified baseline executable, then used for upgrade and downgrade tests.
The upgraded executable preserved every legacy file's SHA-256; the old
executable reused its index and left the new index unchanged. Ordinary RPC
results matched at heights 0, 1, 149, 150 and 700.

`-coinstatsindex=0` remains the default. The unchanged init.cpp conditional does
not construct an index when disabled, so its directory detection and database
creation cannot execute. Disabled startup was tested both by default and
explicitly with legacy data present; there was no new directory, index entry,
index thread or legacy warning. Non-indexed MuHash scans and active chain state
matched across normal restarts. Actual pruned nodes were also tested.

Full and pruned nodes with the index disabled require only the normal executable
upgrade and restart: no migration, reindex, extra history download or manual
maintenance. Chainstate, block-index, wallet, BaseIndex, consensus, parking,
chain selection and earlier backport sources are unchanged.

Enabled full nodes with complete local history rebuild only the new optional
index from genesis. New-index restart resumes without a fresh synchronization.
Enabled pruned nodes missing required history cannot rebuild a new index from
the old one; the existing BaseIndex startup error was tested. Keep the index
disabled or explicitly recover complete history. No automatic redownload occurs.
An already synchronized new index retains existing pruning support.

## Executed validation

Local macOS arm64, existing RelWithDebInfo configuration, Qt configuration,
DATUM, wallet/SQLite, UPnP, ZMQ and tests enabled; BDB disabled. Built bitcoind,
bitcoin-cli and test_bitcoin. An SDK/runtime mismatch in the initial baseline
build called macOS-27 pipe2 on macOS 26.6.2; the crash report pointed to
TokenPipe::Make. Reconfiguring with `-DHAVE_DECL_PIPE2=0` uses the existing pipe
fallback and matches the working local build. No unrelated source fix was made.

TDD baseline: the two existing coinstatsindex cases passed. Both added cases
failed at the missing new-format directory. Compatibility tests failed as
expected when the old executable used the legacy directory instead of the new
one (with both a real legacy DB and an unreadable fixture).

| Check | Final result |
| --- | --- |
| Build bitcoind, bitcoin-cli, test_bitcoin | Passed |
| coinstatsindex_tests | 4 cases passed |
| blockfilter_index_tests, txindex_tests, coins_tests, validation_chainstate_tests | 27 cases passed |
| feature_coinstatsindex.py | Passed; amounts, scan parity, reorg/re-append, clean/unclean restart |
| feature_coinstatsindex_compatibility.py | Passed with unreadable legacy fixture |
| feature_coinstatsindex_compatibility.py --legacy-bitcoind=/tmp/purity30469-old-bin/bitcoind | Passed with actual baseline DB, RPC parity, downgrade, byte preservation, pruned disabled startup and missing-history rejection |
| feature_coinstatsindex_purity.py | Passed; Purity active at height 100, permitted 83-byte OP_RETURN, burned/unclaimed amounts, scan parity, reconnect/restart |
| feature_index_prune.py | Passed unchanged |
| feature_init.py | Passed unchanged |
| rpc_blockchain.py --v1transport / --v2transport | Both passed |
| rpc_dumptxoutset.py | Passed |
| Python compilation, repository-wide changed-field reference review, git diff --check | Passed |

Overflow testing seeds an accounting-consistent persisted state: coinbase at
INT64_MAX-1, other counters containing bits above 128. A real valid block crosses
the signed range, preserves exact counters, subsidy/UTXO/reward accounting, and
survives hash-index lookup, rollback, re-append and restart. Independent wire
fixtures check record width, order and full 256-bit values. RPC tests cover all
three fields at INT64_MAX, INT64_MAX+1, 2^64 and 2^128 (12 boundary cases).

During test development, the RPC fixture needed to drain validation callbacks
before destroying its index; this was corrected in the test. The initial new
Purity functional test omitted getblocktemplate's required segwit rule; it was
corrected and rerun successfully. The initial combined functional run therefore
had six passes and this one test failure; the corrected test passed standalone
and through test_runner. No production code was changed to accommodate either
fixture error. All data used for regression tests were isolated temporary data.

Reproduction commands (run from this worktree; legacy executable must be built
from the unmodified baseline and stored outside build before rebuilding):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5 -DBUILD_GUI=ON \
  -DBUILD_GUI_TESTS=ON -DBUILD_DATUM=ON -DBUILD_TESTS=ON \
  -DWITH_BDB=OFF -DWITH_MINIUPNPC=ON -DWITH_ZMQ=ON -DHAVE_DECL_PIPE2=0
cmake --build build --target bitcoind bitcoin-cli test_bitcoin -j 8
build/bin/test_bitcoin --run_test=coinstatsindex_tests
build/bin/test_bitcoin --run_test=blockfilter_index_tests,txindex_tests,coins_tests,validation_chainstate_tests
python3 build/test/functional/test_runner.py feature_coinstatsindex.py \
  feature_coinstatsindex_compatibility.py feature_coinstatsindex_purity.py \
  feature_index_prune.py feature_init.py rpc_blockchain.py rpc_dumptxoutset.py --jobs=3
python3 test/functional/feature_coinstatsindex_compatibility.py \
  --configfile=build/test/config.ini --legacy-bitcoind=/tmp/purity30469-old-bin/bitcoind
git diff --check
```

## Limits

The existing RPC amount representation remains signed 64-bit satoshis. A single
block whose flow difference exceeds INT64_MAX cannot be represented by that
API; it now returns an explicit error while internal counters remain wide.
Supply-bounded counters retain their upstream types. No RPC redesign is added.

This is local arm64/regtest validation, not a full production mainnet/Signet
index rebuild or Windows/Linux validation. Historical BIP30 helper interfaces
are retained and rollback matches upstream's skip behavior; actual historical
mainnet BIP30 blocks were not replayed during these tests. No fuzz campaign or
full unrelated test suite was run. No deployment was performed.

## Files changed

Implementation:

- src/index/coinstatsindex.cpp
- src/index/coinstatsindex.h
- src/kernel/coinstats.h
- src/rpc/blockchain.cpp

Tests:

- src/test/coinstatsindex_tests.cpp
- test/functional/feature_coinstatsindex_compatibility.py
- test/functional/feature_coinstatsindex_purity.py
- test/functional/test_runner.py

Documentation:

- input/requirements.md
- docs/requirements/product-spec.md
- docs/architecture/coinstatsindex.md
- docs/api/coinstatsindex.md
- docs/migration-report.md
- docs/tasks/TASK-018.md
- docs/tasks/README.md
- docs/INDEX.md
- doc/files.md
- doc/coinstatsindex-backport.md
