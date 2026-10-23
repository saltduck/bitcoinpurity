# TASK-PS-007: Release and full acceptance

Stage: S7. Status: implementation/local acceptance passed; public release pending owner decisions.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Latest local validation: complete macOS and Linux arm64 CTest each 150/150, nine focused functional runs,
82 UBSan cases, 79 identical macOS-arm64/Linux-x86_64 ASERT results and official
Core 28.0 history/UTXO/parallel-operation/pin rehearsal passed. All changes are
uncommitted; no PR, package publication or production deployment occurred.

See ../purity-signet-acceptance.md for the full matrix and the test-only repair
of a pre-existing CoinStats cursor-lock regression. Real public parameters,
independent-host key custody/backups, operated seeds, full future-height history,
remaining release-platform builds and sustained real-time cadence are explicit external gates.

Four-daemon outage/backup recovery and one real-time 600-second interval passed.
The macOS fuzz build is unavailable because Xcode lacks the libFuzzer runtime;
syntax validation passed. Linux Clang fuzz results are recorded in
../purity-signet-review.md.
Operator procedure: ../purity-signet-operations.md.

Linux arm64 node/miner/Mainnet functional acceptance: five cases passed using
container-local wallet directories; shared-mount failure details are preserved
in the acceptance record.

Historical source audit: ../purity-signet-review.md. H=318621 (now superseded), defaults
38333/38332, signet/; final CTest 150/150 and Core28/current-database regressions
passed after those changes.

Linux bounded fuzz passed: 1,907,931 Signet parser cases and 4,202,403 ASERT
cases, both 181-second runs with ASan/UBSan, exit 0. Metadata is preserved in
../purity-signet-fuzz-results.json.

Late-H source and verification: ../purity-signet-late-h-review.md and TASK-PS-008.
Choose H=T+1 at launch; earlier test counts do not certify this new change.
