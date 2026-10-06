# ABC/BCHN parking test port

## Baseline and scope

Working matrix created before editing Purity tests. Production baseline:
`d2e8db6e576e70ebd6cd464e0ce104ea1a8fbc51`.
The user explicitly selected testing the current implementation, retaining the
previous acceptance-time fix, and committing only this task's tests. This is
therefore an invariant baseline for further repairs, not a reconstruction of
the original pre-Issue-1 implementation. Production changes in this task: NONE.

## Current scope decision (2026-10-06)

The user subsequently deferred active-chain manual parking/rewind. That upstream
positive behavior is now excluded from the current scope by an explicit user
decision, not because it failed. Production code remains unchanged. A separate
`active-park-rejected` scenario checks rejection without state mutation. B/D
orderings use inactive branches and retain their state-independence assertions.
Initial failures and commands below remain historical evidence.

## Upstream sources

Both repositories were cloned without shallow history, with blob filtering and
one default branch plus reachable tags. Survey includes current files, their
old/new names, keyword/pickaxe history, validation history and historical diffs.
Exact source revisions (retrieved 2026-10-06):

- ABC: `335857dbf6fc706910c1e888ae3afd3cecf859e0` (A).
  [Repository](https://github.com/Bitcoin-ABC/bitcoin-abc).
- BCHN GitHub mirror: `07576013c91ff4a3a74acd85f189c69121cdad1b` (B).
  [Repository](https://github.com/bitcoin-cash-node/bitcoin-cash-node),
  [canonical GitLab](https://gitlab.com/bitcoin-cash-node/bitcoin-cash-node).

Source files/scenarios:

- A `test/functional/abc_feature_parkedchain.py`: active-chain manual parking,
  unpark by descendant, four invalidity/parking permutations, deep reorgs,
  positive parking log, shallow many-block batch, persisted flags after
  disabling parking, delayed automatic unparking.
- B `test/functional/abc-parkedchain.py`: the same scenarios, with legacy
  parked-vs-invalid RPC precedence after invalidation.
- A/B `src/test/blockstatus_tests.cpp`: `sighash_construction_test`, all six
  validity levels and every data/undo/failed/failed-parent/parked/parked-parent
  combination, individual flag changes and independent mask clearing.
- A `test/functional/rpc_getchaintips.py`: normal active/fork/header/invalid
  visibility; no separate parked scenario beyond the parked-chain file.
- A/B `test/functional/abc-sync-chain.py`: 100 randomly ordered bodies after
  ordered headers, across a minimum-chainwork IBD transition.
- A/B `test/functional/abc-invalid-chains.py`: invalid-parent rejection and
  reconsider/cousin-chain recovery, without parking assertions; inspected for
  collateral history, not recast as a parking port.

Historical sources inspected (shared hashes are present in both histories):

| ID | Commit | Source and relevant change |
|---|---|---|
| H1 | `7878c6b91cacdea6fe4b3de95414bbb8133c5b3a` | `src/test/blockstatus_tests.cpp`; introduces parked-bit permutations |
| H2 | `0e5d128ef3e0d584c54a9ebc2d409f2737e07d87` | status mask clearing tests |
| H3 | `2de68f6eb0f98d0ecc2272f4835590f11b5194d6` | `abc-parkedchain.py`; manual park/unpark |
| H4 | `4a9b35aa30806798214b5d4e33c577a4f16d9df5` | four state ordering regressions |
| H5 | `3cc9d160357adca8e001539f45385197ccae4954` | adds a child and checks flag propagation/order |
| H6 | `638c2fbd9748e9f7f7fddaba068b9f2b835695af` | root must not acquire a spurious parent flag; no new test in commit |
| H7 | `6cc62f82f9e17d6d4cebb649d959de9188017ba9` | parent/child marker mixup; explicitly no new test in commit |
| H8 | `45ca215ceb4f046653142c1e2c4f702bf2a21a69` | explicit RPC child clearing vs auto-unpark; no dedicated added test |
| H9 | `abf0af027c004a99dece4edb6e96fb43b4759d75` | restart/disable parking persists flags, then automatic unpark |
| H10 | ABC `477f66718a833fa0ce33270c11cfa659b5f3ef24`; BCHN `54d94d1a157c2fccfcfe11b6ffeae306213f7b88` | shallow many-block/no-false-parking regression |
| H11 | `e724cfe160dd8358910259ec6ffb328f69f0d703` | `abc-sync-chain.py`; randomly ordered synchronization |
| H12 | ABC `4459a350623b4a31f7a11a62213ee9a461fb54bf` | large-fork warning must clear after manual unpark |
| H13 | ABC `93fea30e948f92a3dee0e0da71b1736ad2688395` | invalid descendants change old parked-status precedence |
| H14 | ABC `5f2017bbb0d1732ea7aaa91555060f7f9ce7b13d` | background IBD must not false-park; production-only, no added test |
| H15 | BCHN `4e9a1738308a16f252e0706f7b5245a082069a7e` | `pindexBestParked` explanatory comment only, not a new regression |
| H16 | ABC `4981f77bb694bfb70129b39a95498615cbf7fc05`, `fc44fd909ab4cc787af736b7b17c392dc6b49a89`, `bb909e76ea8ebfb789de10f63101f60ea7bfdd35` | Avalanche-finalized/voting/miner-fund parking cases |
| H17 | BCHN `1e90a52c75717a6771516065e29b51f4356eea02` | debugging tracepoints only, no added test |
| H18 | `321b2b6363c2df70c7427bd1618fdfbe2de81b2d`, `917d65774c40c6bfad500a660e581c8ea5e20df0` | accumulated-work automatic unpark / depth finalization |

## Port matrix

PORT/ADAPT decisions follow the user's Purity requirements, not current passes.
ALREADY COVERED denotes comparison against actual existing assertions.
The matrix reflects the current deferred scope. Historical failures from the initial port are recorded separately below; no skip or xfail is used.

| Source | Scenario | Classification | Purity test | Result | Notes |
|---|---|---|---|---|---|
| A/B status, H1/H2 | all validity + six status flag combinations | ADAPT | `blockstatus_tests/status_flag_composition` | PASS | raw bit representation plus production `CBlockIndex::IsValid` |
| A/B status, H2 | clear failure/parking independently; toggle each bit | ADAPT | `blockstatus_tests/clear_masks_independently` | PASS | no production helper introduced |
| A/B status | validity changes preserve flags | ADAPT | `blockstatus_tests/validity_changes_preserve_parking` | PASS | production `RaiseValidity` plus raw validity composition |
| A/B parked, H3 | park an earlier active block; rewind; unpark descendant | EXCLUDE | none | N/A | user explicitly deferred active-chain manual parking |
| User scope decision | reject active ancestor/tip without mutation | ADAPT | `--scenario=active-park-rejected` | PASS | Purity current RPC contract; not the upstream positive behavior |
| A/B parked, H4/H5 | invalidate/park/unpark/reconsider (A) | ADAPT | `--scenario=state-a`; C++ `parked_state_order_a` | PASS | boolean parking; no exact upstream status wording |
| A/B parked, H4/H5 | park/invalidate/reconsider/unpark (B) | ADAPT | `--scenario=state-b`; C++ `parked_state_order_b` | PASS | functional and C++ orderings use inactive branches |
| A/B parked, H4/H5 | invalidate/park/reconsider/unpark (C) | ADAPT | `--scenario=state-c`; C++ `parked_state_order_c` | PASS | reconsider must preserve parking |
| A/B parked, H4/H5 | park/invalidate/unpark/reconsider (D) | ADAPT | `--scenario=state-d`; C++ `parked_state_order_d` | PASS | inactive branch; unpark must preserve failure |
| H6/H7 | root-only vs inherited child marker | ADAPT | C++ `parked_root_child_markers` | PASS | not falsely claimed to be an upstream-added test |
| A/B parked, H8 | parked ancestry/descendant unpark restores eligibility | ADAPT | `--scenario=descendants` | PASS | inactive header root; unpark descendant; consensus-valid activation |
| A/B parked | candidate exclusion and root unpark | ALREADY COVERED | existing `deep_reorg_null_block_and_unpark` | PASS | actual candidate membership and activation assertions |
| A/B parked | depths 1/2/5/6/7/8/20 | ADAPT | expanded `feature_park_deep_reorg.py`, C++ acceptance threshold | PASS | Purity strict `rewind > 6`; no automatic recovery expectation |
| A/B parked | custom N/N+1 boundary | ALREADY COVERED | existing functional/custom C++ 4/5 tests | PASS | exact equality checks retained |
| A/B parked, H10 | many blocks with one-block rewind, no false park | ADAPT | `--scenario=batch` | PASS | chain selection + absence of parking log |
| A/B sync, H11 | randomized bodies, no reorg, IBD exit | ADAPT | `--scenario=sync-batch` | PASS | derive minimum work from Purity's chainwork |
| A/B parked, H9 | persisted parked state when disabling new parking | ADAPT | `--scenario=restart` | PASS | many more blocks remain parked until explicit unpark |
| A/B parked | new deep branch with parking disabled | ALREADY COVERED | existing `test_parking_disabled` | PASS | all tip parked booleans false plus most-work activation |
| A/B parked, H10 | automatic parking log | ALREADY COVERED | existing positive-log boundary tests | PASS | match `Parking block`, not upstream wording |
| A RPC/B parked | active/fork/header/invalid/parked visibility | ADAPT / ALREADY COVERED | new scenarios + existing `rpc_getchaintips.py` | PASS | Purity already exposes `parked`; no production RPC edits |
| H12 | no stale warning after resolved fork | INVESTIGATE | descendant-unpark follow-up checks no false parking | PARTIAL / GAP | ABC's Large-fork warning mechanism absent in Purity; no exact-text port |
| H13 / B legacy | parked-vs-invalid status precedence | ADAPT | raw C++ root state + RPC invalidity after unpark | PASS | logical independence, not legacy BCHN status precedence |
| H14 | background snapshot IBD false parking | INVESTIGATE | existing active-body-redownload test is related only | NOT A FULL PORT | no upstream added regression; do not equate with snapshot background coverage |
| H15 | best parked marker cleared | INVESTIGATE | general unpark/selection checks only | NOT A DIRECT PORT | Purity has no `pindexBestParked`; comment-only upstream commit |
| H17 | BCHN parking/finalization tracepoints | EXCLUDE | none | N/A | BCHN-specific instrumentation, not Purity contract |
| A/B parked, H9/H18 | delayed/twice-work automatic unpark | EXCLUDE | opposite invariant in restart scenario | N/A | explicit operator unpark required in Purity |
| H16 | Avalanche votes/finalization/miner-fund policy | EXCLUDE | none | N/A | no Avalanche in Purity |
| B parked, H18 | `-maxreorgdepth` finalization | EXCLUDE | none | N/A | configurable parking is not a hard rejection/finalization limit |
| A/B parked | upstream numeric thresholds | EXCLUDE / ADAPT | Purity 6 and custom 4 boundaries | N/A | invariant ported, upstream numbers excluded |
| A/B parked, H5/H12 | exact RPC/log/error text | EXCLUDE / ADAPT | Purity RPC fields and stable parking log | N/A | no production observability changes |

## Known Issue 1 coverage

No upstream-derived deep competing-chain test found that reproduces the
`pblock`/`pindexMostWork` missing-ancestor-last bypass. Random no-reorg body order
in `abc-sync-chain.py` is not equivalent. The separate Purity-specific
`test_headers_first_missing_ancestor_last` already exists in the selected
baseline; it remains in its original file and is not represented as an upstream
port. Its existing null-body/restart/import cases are likewise retained.

## Search evidence

Local read-only upstream clones: `/tmp/purity-upstream-abc`,
`/tmp/purity-upstream-bchn`. Survey logs in `/tmp/abc-parking-history.log`,
`/tmp/bchn-parking-history.log`, `*-parking-commits.log`,
`*-validation-history.log`, `*-parking-current-search.log`, and
`*-pickaxe-{parkdepth,autounpark,status}.log`.

Commands used in each clone include:

```sh
git rev-parse HEAD
git log --all --format='%H %s' --regexp-ignore-case --grep='park\|deep reorg\|out.of.order.*block'
git log --all --format='%H %s' -- 'test/functional/*park*' src/test/blockstatus_tests.cpp
git log --all --format='%H %s' -- src/validation.cpp
git log --all -S'parkdeepreorg' --format='%H %s' -- 'test/functional/*park*'
git log --all -S'automaticunparking' --format='%H %s' -- 'test/functional/*park*'
git log --all -S'BLOCK_PARKED' --format='%H %s' -- src/chain.h src/blockstatus.h src/test/blockstatus_tests.cpp
git log --all -S'isParked' --format='%H %s' -- src/test/blockstatus_tests.cpp
git show COMMIT -- SOURCE_FILE
git grep -n -E 'parkdeepreorg|automaticunparking|parkblock|unparkblock|BLOCK_PARKED|isParked|hasParkedParent|isOnParkedChain|pindexBestParked|ParkBlock|UnparkBlock|deep reorg|parked chain' HEAD -- src/validation.cpp src/blockstatus.h 'test/functional/*park*' 'test/functional/*invalid*'
```

The survey covers available default-branch/tag history, not unpublished or
unfetched side branches. Current-file links below use the exact A/B hashes; historical hashes are
immutable provenance for adaptations.

## Immutable source links

- [ABC parked-chain scenarios](https://github.com/Bitcoin-ABC/bitcoin-abc/blob/335857dbf6fc706910c1e888ae3afd3cecf859e0/test/functional/abc_feature_parkedchain.py).
- [BCHN parked-chain scenarios](https://github.com/bitcoin-cash-node/bitcoin-cash-node/blob/07576013c91ff4a3a74acd85f189c69121cdad1b/test/functional/abc-parkedchain.py).
- [ABC BlockStatus permutations](https://github.com/Bitcoin-ABC/bitcoin-abc/blob/335857dbf6fc706910c1e888ae3afd3cecf859e0/src/test/blockstatus_tests.cpp).
- [BCHN BlockStatus permutations](https://github.com/bitcoin-cash-node/bitcoin-cash-node/blob/07576013c91ff4a3a74acd85f189c69121cdad1b/src/test/blockstatus_tests.cpp).
- [ABC chain-tip RPC tests](https://github.com/Bitcoin-ABC/bitcoin-abc/blob/335857dbf6fc706910c1e888ae3afd3cecf859e0/test/functional/rpc_getchaintips.py).
- [ABC synchronization regression](https://github.com/Bitcoin-ABC/bitcoin-abc/blob/335857dbf6fc706910c1e888ae3afd3cecf859e0/test/functional/abc-sync-chain.py).
- [BCHN synchronization regression](https://github.com/bitcoin-cash-node/bitcoin-cash-node/blob/07576013c91ff4a3a74acd85f189c69121cdad1b/test/functional/abc-sync-chain.py).

## Tests added or expanded

New C++ cases:

| Case | Invariant |
|---|---|
| `blockstatus_tests/status_flag_composition` | six validity levels times all 64 status combinations; each individual bit toggled; actual `IsValid` unaffected by parking |
| `blockstatus_tests/clear_masks_independently` | clearing failed/parked masks preserves every unrelated flag and validity |
| `blockstatus_tests/validity_changes_preserve_parking` | production `RaiseValidity` across all old/new levels preserves flags; failures prevent raising; raw validity replacement preserves flags |
| `validation_chainstate_tests/parked_state_order_a` | invalidate, park, unpark, reconsider; production calls and activation after each operation |
| `validation_chainstate_tests/parked_state_order_b` | park, invalidate, reconsider, unpark; reconsider preserves parked state |
| `validation_chainstate_tests/parked_state_order_c` | invalidate, park, reconsider, unpark; parked chain remains inactive after failure flags clear |
| `validation_chainstate_tests/parked_state_order_d` | park, invalidate, unpark, reconsider; unpark preserves invalidity |
| `validation_chainstate_tests/parked_root_child_markers` | root has only own parked bit; every descendant only inherited parked bit; no failure flags |

The four C++ state-order cases intentionally use an inactive branch. Their
passes establish core state independence, not support for active-chain manual
parking through RPC. The existing acceptance-threshold case now covers
1/2/5/6/7/8/20, including actual candidate-set membership.

New functional scenarios in `feature_parked_chain.py`:

| Scenario | Invariant | Result |
|---|---|---|
| `active-park-rejected` | RPC rejects active ancestor and tip; active hash and complete tip status remain unchanged | PASS |
| `state-a` | four operations in order A with an active-chain descendant | PASS |
| `state-b` | four operations in order B on an inactive branch; bodies delivered after parking | PASS |
| `state-c` | four operations in order C with an active-chain descendant | PASS |
| `state-d` | four operations in order D on an inactive branch; unpark preserves invalidity | PASS |
| `descendants` | parked header parent propagates to subsequently accepted bodies and higher-work descendants; descendant unpark clears ancestry and fully activates; following block emits no false parking | PASS |
| `batch` | zero/one-block rewind with 20 extra blocks; ordered headers and reversed bodies; storage, best hash, all parked booleans false, no parking log | PASS |
| `sync-batch` | 100 bodies randomly shuffled within requested windows across IBD/minimum-work transition; storage, best hash, IBD exit, no parked tip/log | PASS |
| `restart` | automatic park persists after restart with parking disabled; 30 additional blocks cannot auto-unpark; explicit root unpark restores activation | PASS |

Each scenario is registered separately in the normal runner. No skip or xfail
is used. The rejection case explicitly asserts the supported RPC error and
unchanged state; it does not claim active-chain rollback support.

Expanded `feature_park_deep_reorg.py/test_default_boundary` checks automatic
following at 1/2/5/6 and parking at 7/8/20, parked RPC visibility, positive log,
unchanged active hash and explicit root unpark. Existing custom 4/5,
parking-disabled, ancestor-last, null-body/reconsider, import and reindex cases
remain intact and pass. Existing `rpc_blockchain.py` (both transports) and
`rpc_getchaintips.py` pass.

## Excluded upstream behavior and observability limits

- Accumulated-work/twice-work/delayed automatic unpark and
  `-automaticunparking`: excluded. The adapted restart test instead requires
  the parked branch to remain parked despite additional work.
- Avalanche voting/finalization/miner-fund interactions: excluded.
- BCHN `-maxreorgdepth` rolling finalization: excluded.
- Upstream numeric thresholds: replaced with Purity's strict `rewind > N`.
- Exact upstream RPC strings and legacy BCHN parked-before-invalid status:
  adapted to Purity's independent `parked` boolean plus consensus-invalid
  status. Active, inactive/header and inherited parked branches can be
  identified through the existing boolean; no parking-RPC observability gap
  was found in these scenarios.
- ABC Large-fork warning cleanup (H12): **partial / observability gap** for the
  upstream warning invariant. Purity lacks that warning mechanism/text. The
  follow-up absence of `Parking block` is a separate weaker related check, not
  proof of equivalent warning lifecycle.
- Background snapshot IBD (H14): investigated, not fully ported. The upstream
  fix added no regression; the existing active-body-redownload test is related
  but does not prove snapshot background behavior.
- `pindexBestParked` (H15) and BCHN tracepoints (H17): Purity has no corresponding
  marker/instrumentation; do not invent production APIs to mirror them.

## Historical Purity failures (initial port, before scope change)

All three failures were reproduced individually with unchanged assertions.
They had one common product cause. After the user deferred the feature, the
positive active-park expectation was replaced and B/D were adapted as above.

| Test | Expected assertion/operation | Observed | Relevant production path |
|---|---|---|---|
| `--scenario=manual` | `node.parkblock(root)` succeeds; active hash then equals fallback, descendant `parked=True`, status not invalid | RPC error -1 before rewind/state assertions | `src/rpc/blockchain.cpp:2227` -> `Chainstate::ParkBlock`, `src/validation.cpp:4264-4265` |
| `--scenario=state-b` | initial `parkblock(root)` succeeds; parked descendant inactive; later reconsider preserves parking | same RPC error -1 at first operation; later transitions not reached | same path |
| `--scenario=state-d` | initial `parkblock(root)` succeeds; later unpark preserves failure until reconsider | same RPC error -1 at first operation; later transitions not reached | same path |

Exact observed error:

```text
Cannot park a block on the active chain; use invalidateblock (-1)
```

The current help text also documents this restriction. It conflicts with the
active-chain manual-park requirement initially supplied for this task. `invalidateblock`
is not substituted because consensus-invalidity and local parking must remain
independent. No production fix was made. Later assertions in failed scenarios
are not claimed to have passed merely because equivalent inactive-branch C++
cases passed.

Individual evidence directories and stdout logs:

- `/tmp/purity-parking-manual-individual/test_framework.log` and corresponding
  `/tmp/purity-parking-manual-individual.log`.
- `/tmp/purity-parking-state-b-individual/test_framework.log` and corresponding
  `/tmp/purity-parking-state-b-individual.log`.
- `/tmp/purity-parking-state-d-individual/test_framework.log` and corresponding
  `/tmp/purity-parking-state-d-individual.log`.

## Historical test commands and outcomes (initial port)

Commands recorded from the initial port; `--scenario=manual` is no longer a
current selector. Current verification is recorded in the next section:

```sh
cmake -S . -B build
cmake --build build -j 8
build/bin/test_bitcoin --run_test=blockstatus_tests,validation_chainstate_tests,validation_chainstatemanager_tests --log_level=message
ctest --test-dir build --output-on-failure -j 8
python3 build/test/functional/test_runner.py --jobs=3 feature_parked_chain.py feature_park_deep_reorg.py rpc_blockchain.py rpc_getchaintips.py --tmpdirprefix=/tmp/purity-upstream-functional-verified
python3 test/functional/feature_parked_chain.py --configfile=build/test/config.ini --scenario=manual --tmpdir=/tmp/purity-parking-manual-individual
python3 test/functional/feature_parked_chain.py --configfile=build/test/config.ini --scenario=state-b --tmpdir=/tmp/purity-parking-state-b-individual
python3 test/functional/feature_parked_chain.py --configfile=build/test/config.ini --scenario=state-d --tmpdir=/tmp/purity-parking-state-d-individual
python3 test/functional/feature_parked_chain.py --configfile=build/test/config.ini --tmpdir=/tmp/purity-parking-all-individual
python3 -m py_compile test/functional/feature_parked_chain.py test/functional/feature_park_deep_reorg.py
git diff --check
```

| Check | Outcome | Log |
|---|---|---|
| Configure, existing build configuration (GUI enabled, DATUM disabled) | exit 0 | `/tmp/purity-upstream-configure.log` |
| Full normal build | exit 0 | `/tmp/purity-upstream-full-build-final.log` |
| Focused C++ suites | 27 cases pass, exit 0 | `/tmp/purity-upstream-unit-final.log` |
| Full CTest including Qt tests | 149/149 pass, exit 0 | `/tmp/purity-upstream-ctest-final.log` |
| Functional runner | 10 pass / 3 fail, exit 1; six of nine new scenarios pass | `/tmp/purity-upstream-functional-verified.log` |
| Three individual failed scenarios | each exits 1 with the error above | individual logs above |
| Default functional script, all scenarios | exit 1 at manual; independent variants establish remaining results | `/tmp/purity-parking-all-individual.log` |
| Python compilation and diff whitespace | pass, exit 0 | command output |

Two test-port/setup errors were corrected, not counted as product failures:

1. CMake's functional-file glob needed reconfiguration to create the new script
   symlink; the initial launch failures did not execute test scenarios.
2. The original helper incorrectly required `submitblock == null` for a parked
   body. Core's `src/rpc/mining.cpp:1170-1171` returns `inconclusive` when no
   `BlockChecked` event is emitted because the block remains parked. The helper
   accepts null or inconclusive and still verifies exact stored body bytes.
   Parked-state and complete post-unpark activation assertions are unchanged.

The randomized sync port respects Core's requested in-flight windows and
minimum-chainwork anti-DoS handling. It does not claim arbitrary unrequested
low-work bodies must be retained. Randomness uses the framework's recorded seed.

No imported ABC/BCHN test reproduces the known Issue 1 deep-fork
`pblock`/selected-tip mismatch. A separate Purity-specific regression is needed
for that invariant; in the user-selected baseline it **already exists and
passes**, so no newly invented regression was mixed into this port.

## Current verification after deferral

Commands:

```sh
python3 build/test/functional/test_runner.py --keepcache --cachedir=/tmp/purity-parking-defer-after-cache --jobs=3 feature_parked_chain.py feature_park_deep_reorg.py rpc_blockchain.py rpc_getchaintips.py --tmpdirprefix=/tmp/purity-parking-defer-after
python3 test/functional/feature_parked_chain.py --configfile=build/test/config.ini --portseed=781 --tmpdir=/tmp/purity-parking-defer-all
python3 -m py_compile test/functional/feature_parked_chain.py test/functional/test_runner.py
git diff --check
```

Results: all **13/13** functional variants pass (nine parking scenarios plus
existing deep-reorg, two blockchain transports and chain-tip RPC tests). The
default combined script also passes all nine scenarios. Python compilation and
whitespace checks pass. Logs: `/tmp/purity-parking-defer-after.log` and
`/tmp/purity-parking-defer-all.log`. Before adaptation, the nine-scenario runner
reproduced the original 6-pass/3-fail result in
`/tmp/purity-parking-defer-before.log`.

Production code and existing C++ tests remain unchanged. The full functional
suite was not rerun for this scope adjustment.

## Production code changes

**NONE.** Only test sources, test registration and this provenance/result report
were changed. The prior acceptance-time implementation remains the selected
baseline. Passing this port does not restore or prove the behavior of an older
pre-fix revision.
