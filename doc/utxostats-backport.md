# UTXO statistics race backport and verification

Baseline: clean master `789f3781a79174bbb4a9fa75efa21c1e0a58f30b` in an
isolated worktree; task branch `codex/backport-core-34451-34908`.

Exact merged upstream patches reviewed in order:

- [Core #34451](https://github.com/bitcoin/bitcoin/pull/34451), merge
  `4169e72d9ed6320251feea821eb7c047793a50bc`; constituent commits
  `5e77072fa60110a00d2bff31798d58b6c10bd3da` and
  `f3bf63ec4f028cf9ee0820226f44fcbe26d358c9`.
- [Core #34908](https://github.com/bitcoin/bitcoin/pull/34908), merge
  `b6d1b65062ab123248e4f209fe3f63118f03bad6`; constituent commit
  `3e5dc610353ef16b0e7f391faaac77da6f4a0fab`.

## Implementation

#34451 removes the RPC's early current-tip block-index capture, guards the
historical index synchronization check, and uses stats.hashBlock to find the
indexed result's parent. Cursor acquisition and lookup of its recorded block
are performed in one cs_main critical section, before scanning the cursor.

#34908 (planned next commit) moves cursor creation, block lookup and CCoinsStats construction into
the internal template, which returns optional<CCoinsStats>. The public function
dispatches the three hash modes; the RPC pindex declaration moves to its first
use. Cursor read failures remain nullopt, interruptions propagate exceptions,
and the estimated disk size continues to describe the view, not necessarily
the cursor's snapshot if a flush occurs while scanning.

Knots 29.4 adaptations: retain ApplyStats(stats, prevkey, outputs), the existing
request.params-based hash parsing and public function signature. No unrelated
upstream argument, Index, CoinsView, BlockManager or ChainstateManager API
updates are imported. #30469's arith_uint256 cumulative accounting, wide RPC
differences, INT64_MAX checks, fields and numeric format are preserved verbatim.
No changes to index serialization, directories or legacy migration rules.

## Lock and lifetime review

- One cursor is created per hash dispatch under cs_main. Lookup uses only its
  GetBestBlock; no separate view best-block read selects scan metadata.
- The scan runs outside the newly acquired critical section. A caller already
  holding recursive cs_main retains its outer lock, including existing
  background AssumeUTXO validation. This behavior is tested and preserved.
- The phase-one unique_ptr move transfers ownership exactly once. The final
  helper owns the cursor/LevelDB iterator until success, failure or exception.
- Existing BlockManager entry and chainstate/CoinsDB lifetime assumptions are
  retained. The RPC already takes a database pointer and scans after releasing
  cs_main; these PRs add no chainstate replacement or destruction protection.
  General concurrent database replacement remains outside this backport; this
  review does not establish a new concrete lifetime defect.
- cs_main -> cursor/DB acquisition follows existing flush/snapshot callers;
  no additional lock is acquired or lock-order inversion introduced. Existing
  callers holding cs_main throughout long scans remain outside this scope.

## Compatibility

Production files are limited to src/kernel/coinstats.cpp and
src/rpc/blockchain.cpp. Hash serialization/finalization, UTXO iteration order,
counting, amount accumulation and snapshot serialization are unchanged. ASERT,
RDTS/BIP110, activation pinning, deep-reorg parking, validation, mempool/mining
policy, wallet and P2P source is untouched. No #34521 is imported.

No new index requirement is added: disabled full/pruned nodes and use_index=false
continue scanning without creating an index. These two PRs require no reindex,
chainstate rebuild, database migration or new index rebuild, including nodes
already using #30469's index format. Its prior legacy/new-format distinction
remains applicable. No deployment or remote push is part of this task.

## Validation

Stage one is locally validated. Stage two is pending. Build configuration: macOS arm64, RelWithDebInfo,
wallet/SQLite, DATUM, UPnP, ZMQ and tests enabled; GUI/BDB disabled. Configure
with HAVE_DECL_PIPE2=0 to use the existing macOS-compatible fallback. Production
sources contain no platform workaround or scheduling delay.

The added coinstats_tests simulate a block advance and flush precisely at
cursor creation, verify metadata/content consistency and one locked acquisition,
exercise an externally held recursive cs_main, flush during a scan, and cover
key/value failures and interruptions for every hash mode. The added RPC index
test exercises current versus historical errors before background sync starts.

feature_utxostats_race uses independent RPC connections and bounded barriers
for 40 rounds per index mode, with mining and all three hash queries released
together. Every error fails the test; no RPC exception is ignored. Indexed
results are compared to historical hash queries after the concurrent run.
Actual pruning, all-mode full/pruned parity, absence of index directories and
stable indexed/non-indexed field comparisons are also checked. Functional
overlap is exercised; only the unit fixture deterministically places a state
transition at the original vulnerable boundary.

Baseline TDD: cursor_snapshot produced 12 failures across the three modes
(unlocked cursor acquisition, height/hash mismatch and a separate best-block
read). coinstatsindex_rpc_syncing produced one expected predicate failure.
Other added failure/interruption/flush checks passed on the baseline. The
existing feature_utxo_set_hash passed before production changes.

| Check | #34451 stage | #34908 stage |
| --- | --- | --- |
| bitcoind, bitcoin-cli, test_bitcoin build | Passed | Pending |
| coinstats_tests, coinstatsindex_tests, blockfilter_index_tests, txindex_tests, coins_tests, validation_chainstate_tests, validation_chainstatemanager_tests | 50 cases passed | Pending |
| feature_utxostats_race | Passed | Pending |
| rpc_blockchain --v1transport / --v2transport | Both passed | Pending |
| feature_utxo_set_hash | Passed | Pending |
| feature_coinstatsindex | Passed | Pending |
| feature_coinstatsindex_purity | Passed | Pending |
| feature_coinstatsindex_compatibility | Passed | Pending |
| feature_index_prune | Passed | Pending |
| feature_assumeutxo | Passed | Pending |
| rpc_dumptxoutset | Passed | Pending |
| git diff --check | Passed | Pending |

The first combined functional run had nine successful tests and one launch
failure: feature_utxostats_race was added after CMake's initial configure-time
file glob, so its build-directory link did not exist. Reconfigured/rebuilt;
the new test then passed through test_runner. No RPC errors were ignored.

Fixed UTXO commitments match before/after #34451:
hash_serialized_3 = d1c7fec1c0623f6793839878cbe2a531eb968b50b27edd6e2a57077a5aed6094;
MuHash = d1725b2fe3ef43e55aa4907480aea98d406fc9e0bf8f60169e2305f1fbf5961b.
The independent Python MuHash calculation also agrees. The full AssumeUTXO
suite exercises creation/loading, invalid and valid hashes, activation,
background validation and pruning. Existing CoinStatsIndex tests cover
historical height/hash, amounts/unspendable categories, reorg/re-append,
restart and index/scan parity; unit tests retain #30469 wide-record and RPC
boundaries. The added pre-sync RPC case covers current/historical errors.
Compatibility uses the unreadable legacy fixture; an external pre-#30469
executable upgrade/downgrade is not rerun because no index storage changed.

Commands (run each stage after building, never concurrently with relinking):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DWITH_BDB=OFF \
  -DWITH_MINIUPNPC=ON -DWITH_ZMQ=ON -DHAVE_DECL_PIPE2=0
cmake --build build --target bitcoind bitcoin-cli test_bitcoin -j 8
build/bin/test_bitcoin --run_test=coinstats_tests,coinstatsindex_tests,blockfilter_index_tests,txindex_tests,coins_tests,validation_chainstate_tests,validation_chainstatemanager_tests
TEST_RUNNER_PORT_MIN=31000 python3 build/test/functional/test_runner.py \
  feature_utxostats_race.py rpc_blockchain.py feature_utxo_set_hash.py \
  feature_coinstatsindex.py feature_coinstatsindex_purity.py \
  feature_coinstatsindex_compatibility.py feature_index_prune.py \
  feature_assumeutxo.py rpc_dumptxoutset.py --jobs=3
git diff --check
git diff --cached --check
```

## Changed files and limits

Production: src/kernel/coinstats.cpp, src/rpc/blockchain.cpp.
Tests: src/test/coinstats_tests.cpp, src/test/coinstatsindex_tests.cpp,
src/test/CMakeLists.txt, test/functional/feature_utxostats_race.py,
test/functional/test_runner.py. Requirements, architecture, API, migration,
TASK-020/TASK-021, documentation indexes and this report are synchronized.

Validation is local macOS arm64/regtest. Linux/Windows, production mainnet
replay, an external legacy executable and a fuzz/sanitizer campaign were not
run. No tests in the targeted group were intentionally skipped. This does not
prove the absence of all scheduling or lifetime races beyond these two PRs.
