# Acceptance-time deep-reorganization parking

Scope: parking only. RDTS, replay protection,
manifests, PoW and transaction consensus are unchanged by this follow-up.
The prior parking implementation in TASK-014 is superseded.

## Root cause

`pblock` in `ActivateBestChain` is an optional disk-read optimization, not a
chain-selection identity. After all headers and descendant bodies arrive, a
missing ancestor received last can recursively link the descendant tip. The
incoming ancestor is then not the selected most-work tip. The original
`pblock && pblock->GetHash() == pindexMostWork->GetBlockHash()` predicate skips
parking in that ordinary delivery order and also for null-body activation.

The intermediate fix checked depth at activation and required a special
operator approval mode. This follow-up instead decides parking in acceptance
and removes that mode entirely.

## New architecture

`ChainstateManager::AcceptBlock` performs header/contextual validation and
stores the body, then evaluates parking immediately before
`ReceivedBlockTransactions`. Under `cs_main`, it uses only the current active
tip, incoming competing block index, active-chain fork and configured depth.
Blocks on the active chain are excluded, including redownloaded pruned bodies. When
`active_tip.height - fork.height > parkreorgdepth`, the first competing block
after the fork is marked `BLOCK_PARKED` and added to the dirty index set. This
root can be a header index whose body is still missing. An already parked root
is not logged/marked again for each received descendant.

`ReceivedBlockTransactions` processes linked parents before their descendants.
`TryAddBlockIndexCandidate` propagates a parent's parked mask to the child,
persists `BLOCK_PARKED_CHILD`, and excludes it from candidates. Startup index
loading is likewise sorted by height. Propagation checks the immediate parent,
avoiding a full-ancestry scan for every block during reindex.
`FindMostWorkChain` retains its full branch-path check, including lazy removal
of candidates whose root was parked after they were inserted. Reprocessing
already stored bodies does not clear parked status.

No automatic depth determination remains in `ActivateBestChain`; `pblock`
only avoids a disk read when it matches the selected block.

## Delivery-order regression

The functional test creates A with a 7-block rewind and B with 8 competing
blocks and greater cumulative work. It sends all B headers, verifies that
headers alone do not trigger parking, withholds B1, and sends B2 through Bn.
Every descendant is confirmed stored while A remains active. The RPC must
already report Bn as parked before B1 arrives. B1 arrives last without another
parking log; the newly linked most-work branch still cannot activate.

The original activation-time matching-body predicate cannot satisfy this test:
with B1 missing, descendants are unlinked and the parking decision is skipped;
when B1 arrives, `pblock` is B1 while the most-work tip is Bn. The test also fails
against TASK-014's later activation-time implementation at the earlier assertion
requiring parking before the missing ancestor arrives.

## Manual unpark

`UnparkBlock` clears relevant descendant and ancestor masks before restoring
candidates. Only blocks with connected transaction ancestry are restored;
clearing a parked header or an unlinked body must not turn it into a candidate.
`unparkblock` then calls ordinary `ActivateBestChain(state, nullptr)`. There is
no `DeepReorgPolicy`, bypass enum or special approval argument. With all bodies
available, the most-work branch activates and its RPC `parked` value is false.
Newly accepted bodies are evaluated against the current active tip; clearing
parking before missing bodies arrive is not persistent approval of future data.

## Startup, import and reindex

- Normal restart loads persisted parking masks; null-body activation honors
  them. `reconsiderblock` clears invalidity, not parking.
- `-reindex-chainstate` retains the block index and its parking masks. The test
  confirms A remains active and B parked after this rebuild.
- `-loadblock` uses `LoadExternalBlockFile` and the same `AcceptBlock` as live
  receipt, followed by null-body activation. The regression persists all B
  headers, writes B2..Bn then B1 to a raw bootstrap file, restarts with
  `-loadblock`, and checks that A remains active until manual unpark.
- Full `-reindex` reconstructs index and chainstate status from scratch, so
  previous parking decisions are discarded. During an unpruned import only
  genesis is initially active; accepting those blocks does not require a deep
  rewind. Final most-work selection can activate a previously parked branch.
  A dedicated scenario verifies this reset. This is distinct from automatic
  unparking of an existing index. Pruned reindex/pruning-specific scheduling is
  not dynamically covered here.
- Enabling parking later does not retroactively classify every already stored
  body. Acceptance-time decisions require an established active tip/fork;
  historical branch selection in a rebuilt/empty chainstate is not a deep
  rewind of the operator's previous active chain.

## RPC and compatibility

The previously added `getchaintips.parked` boolean is retained on every tip.
It is true when the tip or any ancestor has `BLOCK_PARKED_MASK`, including a
parked header root with no body. Existing status strings are unchanged.
Tests check false on the active tip, true on the competing tip, and false
after unpark/activation. `parkblock` and `unparkblock` remain supported.

Mainnet parking defaults on and test-chain defaults off are unchanged.
`parkreorgdepth` is still at least 1, default 6, with parking only for strictly
greater rewinds: 6 is allowed, 7 is parked; custom depth 4 allows 4 and parks 5.
Parking is local chain-selection policy. No parked branch is made invalid by
parking, and header/PoW/transaction validity is unchanged. No Avalanche,
automatic unparking, work-based clearing or automatic finalization is added.

## Tests and execution

C++ `validation_chainstate_tests` changes:

- `deep_reorg_acceptance_threshold`: 1/6/7/20 at depth 6, checks flags and
  candidate exclusion immediately after acceptance, before activation.
- `deep_reorg_custom_acceptance_threshold`: 4/5 at depth 4.
- `deep_reorg_acceptance_disabled`: depth 20 activates with parking disabled.
- `deep_reorg_null_block_and_unpark`: parked root/child exclusion, repeated
  candidate insertion, body reprocessing, flag clearing and normal null-body
  activation with no override.
- `deep_reorg_delivery_order`: both topological and ancestor-last delivery
  produce the same root/descendant flags and candidate exclusion.
- `deep_reorg_active_body_redownload`: active-chain pruned-body metadata is
  reconstructed and the historical body is accepted without parking.
- `deep_reorg_unpark_missing_body`: automatic header-root parking plus manual
  parking of an unlinked descendant; unpark does not insert incomplete blocks;
  accepting the missing body applies policy again, then normal unpark activates.

The test fixture now honors custom `parkreorgdepth` as well as the existing
parking enable argument. Existing mainnet/test-chain defaults, option range
and custom-threshold tests in `validation_chainstatemanager_tests` remain.

`feature_park_deep_reorg.py` retains normal 6/7 and custom 4/5 boundaries and
invalid-option/disabled tests, strengthens the ancestor-last test, replaces
activation-time retroactive reconsider parking with persisted parking across
restart/reconsider/reindex-chainstate, and adds deterministic out-of-order
bootstrap import and full-reindex reset scenarios. RPC assertions accompany
these cases; `rpc_getchaintips.py` and `rpc_blockchain.py` are also run.

TDD evidence: before the implementation, the revised C++ suite failed
acceptance-time assertions and the P2P test failed because Bn's `parked` was
false while B1 was missing. A separate unlinked-unpark regression then failed
because the incomplete descendant entered the candidate set; its fix limits
restoration to blocks with connected transaction ancestry. The active-body
redownload test also caught a null-root access when the incoming index itself
was the active-chain fork; the competing-branch check prevents it.

Verified locally on 2026-10-06 against the final sources. The existing build
configuration is macOS/Qt 5, `BUILD_GUI=ON`, `BUILD_DATUM=OFF` and
`HAVE_DECL_PIPE2=0`. Commands were executed from the repository root:

```sh
cmake --build build -j 8
build/bin/test_bitcoin --run_test=validation_chainstate_tests,validation_chainstatemanager_tests --log_level=message
python3 build/test/functional/test_runner.py --jobs=3 feature_park_deep_reorg.py rpc_blockchain.py rpc_getchaintips.py feature_loadblock.py feature_reindex.py --tmpdirprefix=/tmp/purity-parking-functional-verified
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -j 4
git diff --check
```

Results: full build passed; focused C++ tests **19/19** passed; functional runner
**6/6** passed (the five named scripts include two `rpc_blockchain` transport
variants); full CTest **148/148** passed, including Qt. Diff whitespace check
passed. No source modifications to dependencies, RDTS, replay protection,
manifests or unrelated consensus were made by this follow-up.

Logs retained locally:

- `/tmp/purity-parking-build-final.log`
- `/tmp/purity-parking-unit-final.log`
- `/tmp/purity-parking-functional-final.log`
- `/tmp/purity-parking-ctest-final.log`

The focused functional script was also executed directly during TDD:

```sh
python3 test/functional/feature_park_deep_reorg.py --configfile=build/test/config.ini --tmpdir=/tmp/purity-parking-acceptance-before
python3 test/functional/feature_park_deep_reorg.py --configfile=build/test/config.ini --tmpdir=/tmp/purity-parking-acceptance-after
```

The first failed on the old activation-time implementation; the second passed
after moving the decision. The final runner above covers all subsequent fixes.
The negative-control C++ commands included:

```sh
build/bin/test_bitcoin --run_test=validation_chainstate_tests --log_level=message
build/bin/test_bitcoin --run_test=validation_chainstate_tests/deep_reorg_unpark_missing_body --log_level=message
build/bin/test_bitcoin --run_test=validation_chainstate_tests/deep_reorg_active_body_redownload --log_level=message
```

Each detected its targeted defect before that fix. Final successful runs used
restored/current sources, with no temporary old implementation left in place.
This is local verification, not CI, Linux/Windows testing or a production
rollout. No push or deployment was performed.
