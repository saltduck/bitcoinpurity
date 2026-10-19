# Late Signet activation: implementation and review

Implementation baseline: `13031062d2` (the Signet implementation already present
at task start). This change selects H immediately before mining. H=318621 and
its archived boundary data are historical rehearsals, not launch parameters.
All work is local; no commit, PR, public deployment or official H selection is
part of this task. Chinese operational manuals remain outside the repository.

## Implemented boundaries

- The existing Signet identity, magic, directory and defaults remain unchanged.
  Schema v2 explicitly requires all four activation fields null in preparation;
  common fields remain strictly checked. Fixed v1 files retain compatibility;
  fixed v2 anchors must be H-5. Preparation uses the inactive height sentinel,
  original challenge and Bitcoin difficulty rules, and blocks both mining RPCs
  and internal assembly. RPC reports preparing with null activation fields.
- prepare.py checks readiness and synchronization, pauses P2P, scans all stored
  blocks and undo data with verifychain(2,0), then runs verifychain(4,288) for
  recent UTXO disconnect/reconnect consistency. Completed unpruned sync with
  assumevalid=0 provides full historical contextual and script validation; the
  bounded level-4 window avoids a history-sized UTXO overlay. It samples the
  active tip and anchor, rechecks snapshot stability, records actual
  hashes/bits/time/chainwork and both verification results, requests normal
  stop and creates a non-overwriting fixed file. Parent age is checked after
  verification against wall time, default 3600 seconds. It changes no consensus
  timestamps or target.
- A Signet-only block-index record stores the consensus fingerprint and the
  first-block pin. Preparation may become fixed; a fixed rule tuple cannot be
  changed without restoring preparation state or explicit full reindex. The
  first hash can be added once; it cannot subsequently be removed or replaced.
- Loading fixed consensus rechecks stored header constraints. On conversion,
  cached H+ script validity is downgraded and stored data is rechecked. Invalid
  branches and descendants cannot enter candidates or best-header selection.
  Active UTXO/replay heads are checked before replay. Incompatible active state
  fails instead of inheriting a cached legacy verdict. Missing legacy context
  with active H+ requires explicit chainstate revalidation.
- Revalidated existing indexes are synchronously persisted before the new rule
  fingerprint is written. This ordering prevents a crash from publishing the
  new context while retaining stale validation flags. Fresh databases do not
  prematurely persist a newly written genesis through this conversion flush.
- Independent two-wallet mining now rehearses the actual preparation/freeze/
  restart pipeline. First-block pinning, restart, both reindex modes and fresh
  synchronization use the existing fixed-height consensus and actual GBT bits.
  No chain marker, first-seen activation or new chain-selection priority exists.

## Findings resolved during testing and review

1. Schema v2 preparation originally failed the reader's version check. The
   new regression covers v1 compatibility, null groups, missing fields, mixed
   pins, common validation and v2 H-5 enforcement.
2. The prior index loop refused the entire database on any conflicting stored
   pin, including inactive branches. Failure is now per branch; incompatible
   active history still fails before replay/UTXO adoption.
3. Clearing failure flags with reconsiderblock could revive a cached legacy
   branch and cause unparkblock to hit a signed-block read error. Reconsideration
   now rechecks Signet constraints/data before restoring candidate eligibility.
4. A second regression retained a failed legacy parent with a separately valid
   descendant header. Reconsideration cleared the descendant flag and let it
   enter best-header statistics. Reapply retained ancestor failures in height
   order and remove those descendants from candidates before returning.
5. Source review found a durability ordering window between storing the rule
   fingerprint and flushing revalidated index flags. Persist existing rechecked
   indexes first; abrupt-exit regression accompanies the ordering check.
6. A rejected wrong first-block pin must not poison the already accepted active
   chain. The test restarts successfully with the correct pin, and also rejects
   later pin removal and reversal to preparation.

## Mainnet isolation

`CMainParams`, all of src/pow.cpp, src/consensus/tx_verify.cpp and
src/deploymentstatus.h are byte-identical to the implementation baseline.
Thus this change does not modify the original ASERT formula, 224-bit assertion,
Mainnet parameters or ordinary BIP110 output-size checks. New parsing, state,
metadata, revalidation and mining rejection paths are Signet-limited. The
non-Signet activation-pin startup failure is retained.

The local comparison record is build/signet-late-h-mainnet-proof.json. Source
comparison and finite regression/fuzz runs are evidence for these boundaries,
not a formal proof over every possible consensus input.

## Validation

The low-memory verification policy update was added after the original evidence
below. Its Python regression suite passes 10 cases, including both full-history
scan and recent chainstate failure paths. The functional freeze test also checks
the recorded verification depths and actual number of recent blocks checked.
Linux sanitizer and fuzz campaigns below predate this operational-script-only
change.

| Acceptance area | Final evidence | Result |
| --- | --- | --- |
| v1 compatibility / strict v2 preparation | chainparams unit suite, missing/mixed fields and H-5 checks | Passed |
| Snapshot consistency / stale/unsynced/failed verification | 9 Python unit cases registered as one CTest; real-prefix freeze tool execution | Passed |
| Preparing sync / mining rejection | Three nodes, actual original blocks 1..10 over P2P; templates/proposals/generation refused; standalone miner refuses preparation | Passed |
| Same directory / legacy data and heads | Cached original H full block, competing H header and H+1 descendant; abrupt exit, reconsider/unpark and best-header exclusion | Passed |
| Wrong pin / official pin / database recovery | Incorrect H pin rejected without poisoning accepted state; correct pin restart, both reindexes, prepared-peer and fresh-node sync/UTXO equality; pin removal/preparation reversal refused | Passed |
| Complete independent mining workflow | Prepare node, create independent signing wallets, freeze with tool, restart fixed configuration, mine real signed PoW; signer outage, alternate quorum and wallet backup recovery | Passed |
| H / H+1 / RDTS / ASERT | Existing Purity Signet and mining regressions, ordinary-output negative checks, signature/witness mutation and integer ASERT comparisons | Passed |
| Native macOS arm64 | 151/151 CTest entries; 6/6 selected functional cases | Passed |
| Linux arm64 / Ubuntu 24.04 | 151/151 CTest entries; the same 6/6 functional cases with container-local state | Passed |
| Original Core 28.0 comparison | Shared prefix and UTXO, distinct H continuation, pinned restart, incompatible original post-H active state rejection | Passed |
| Linux libFuzzer + ASan + UBSan | signet: 270702 executions / 61s; signet_asert: 751364 / 61s; total 1022066; both exit 0 | Passed |
| Mainnet boundaries | Byte comparison to task baseline; CalculateASERT/GetNextASERTWorkRequired and entire tx_verify.cpp also match cc85cfad90 exactly; mining_mainnet on both platforms | Passed |

The six functional scripts are feature_signet, feature_signet_prepare,
feature_purity_signet, p2p_purity_signet, tool_purity_signet_miner --descriptors,
and mining_mainnet. The miner test now exercises preparation and tool-generated
parameters before actual two-wallet signing. Cadence checks use mocktime; this
run is not new evidence of a real 600-second public operating interval.

Final logs are build/signet-late-h-release-{ctest,functional,reference}.log and
build/linux-arm64/late-h-release-{ctest,functional}.log. Native CTest took
226.31 seconds; Linux CTest took 240.89 seconds. Functional batches took 72 and
118 seconds respectively. Local binary/log checksums and comparison details
are saved in build/signet-late-h-results.json. Bounded fuzz parameters, compiler,
source/binary/log checksums and reported RSS are saved in
[purity-signet-late-h-fuzz-results.json](purity-signet-late-h-fuzz-results.json).
No sanitizer error or assertion failure was reported. Fuzz's ordinary PoW
wrapper is simplified; actual mining evidence comes from normal node/solver
functional binaries (Linux node BUILD_FOR_FUZZING=OFF), not that fuzz wrapper.
Index migration is covered by functional tests rather than these fuzz targets.

Initial attempts remain separately logged. Reader preparation and descendant
best-header regressions failed before their respective fixes. One overlapping
functional batch collided with an older test's RPC ports; isolated reruns passed.
The unintentionally selected real-cadence variant is not counted as passing
acceptance. No production check was relaxed for those harness issues.

## Operational acceptance still required

Official authorities and operated seeds remain simulated in the sample. Leave H
null until launch readiness; the tool must operate on actual fully synchronized
unpruned history with sufficient verification cache. Readiness is an operator
attestation of signer custody/endpoints, not remote identity authentication.
Stop other RPC import writers while freezing, wait for clean daemon exit and
back up preparation state before the first fixed-parameter restart. Recheck age
if signing/restart is delayed; restore preparation backup and resynchronize to
choose another H. Configuration is not reloaded online.

The small real-history fixtures do not constitute validation of the complete
current original Signet chain or public deployment. Existing shared-magic,
replay/address-format and operational peer-diversity risks remain. Full official
history/UTXO evidence, real signer recovery and public network monitoring must be
recorded during launch. No Mainnet changes or production key material are added.
