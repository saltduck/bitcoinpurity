# TASK-PS-008: Freeze H immediately before mining

Status: implemented and locally validated; official deployment deferred.
Dependencies: TASK-PS-001..006. Release evidence: TASK-PS-007.

1. Extend the strict reader with schema v2: complete null preparation group,
   otherwise fixed H/H-5; retain v1 compatibility and common validation.
   Preserve chain identity, ports and directory. Expose preparing state/nulls,
   original historical consensus and mining/assembly rejection.
2. Add prepare.py: readiness confirmation, full synchronization, paused P2P,
   verifychain(4,0), consistent active snapshot, configurable one-hour age check,
   exact H=T+1/A=H-5/parent time/bits/chainwork, evidence checksums, normal stop
   and non-overwriting fixed output. Never change timestamps or difficulty.
3. Record the Signet rule fingerprint in the block index. On conversion recheck
   H+ headers/data, reset cached script validity for candidate connection,
   isolate inactive conflicts/descendants and validate active/replay heads.
   Keep reconsider/unpark from reviving incompatible cached data. Preserve
   Mainnet's existing pin behavior and consensus paths.
4. Reuse two-wallet mining and first-block pinning. Exercise restart, both
   reindex modes, fresh synchronization and failures. Update requirements,
   product, architecture, API, operations and migration records in English.
   Keep both Chinese manuals outside the repository.
5. Run native/Linux unit and functional regressions, Signet/ASERT libFuzzer
   under ASan+UBSan, and compare untouched Mainnet consensus source. Record
   actual evidence separately from formal launch acceptance.

Acceptance: preparation sync and blocked mining; invalid null groups rejected;
consistent parameter generation; stale/unsynced snapshot rejection; same-directory
conversion with full inactive old blocks and headers; pins/replay/cache safety;
H boundary and H+1 ASERT; pin/reindex/fresh sync; signer recovery and duplicate
submission; Mainnet invariants; Linux regressions and sanitizer/fuzz logs.


Executed acceptance: native macOS arm64 and Linux arm64 each passed 151/151
CTest entries and 6/6 selected functional cases. Core 28.0 comparison passed.
Final Linux Signet/ASERT libFuzzer campaigns completed 1022066 executions under
ASan+UBSan with exit 0. Full evidence and operational boundaries:
[late-H review](../purity-signet-late-h-review.md).
