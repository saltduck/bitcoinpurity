# TASK-046: Generic SV2 regression and interoperability evidence

Status: local validation complete; native Windows and release gates remain open.
Production corrections are separate commits: TASK-040 0c2ac178c6,
TASK-041 b5ea9ab01b, TASK-042 d86da0de63, TASK-043 44e4cf6ebb,
TASK-044 59679f8400, TASK-045 045cb57606. This task changes tests/CI and records
results; it does not change production gateway behavior.

## Local results (2026-11-02, Australia/Melbourne)

- Linux Ubuntu 24.04 ARM64 and macOS ARM64: full enabled builds, 157/157 CTests
  including Qt and Core v32.1 schema checks, and 20/20 locked Rust tests pass.
- Both platforms: six selected functional scenarios pass (SV2, DATUM, GUI IPC,
  open-peer shutdown/reindex, mining_basic, rpc_misc). The default SV2 scenario
  retains empty tags and 16-byte extranonce. Additional full-chain runs with
  tags and 12/32-byte extranonce pass on both platforms, including command-line
  tags overriding configuration-file tags in the actual mined coinbase.
- SV2 scenarios calculate ordinary share and network-block PoW in an external
  V1 software miner, observe pool acknowledgement and propagation to a second
  node. Existing same-parent delayed work, duplicate/invalid shares, reorg,
  wrong authority, explicit Setup errors, timeout and outage assertions remain.
- Both GUI/daemon disabled builds pass; their caches configure nonexistent Rust
  compiler paths. Both local release packaging/extraction and GUI IPC/PoW tests
  pass, including native Cocoa from the extracted macOS application.
- Sixteen IPC/miner/cache ASan+UBSan tests pass. Both unmodified IPC dependencies
  use matching instrumentation. Only unsupported macOS leak detection is disabled.
- Strict Rust 1.96.0 Clippy still fails with the same seven diagnostic classes
  as a fresh be1821d9e7 baseline (five missing unsafe safety sections, unit error
  result and client argument count). No lint is suppressed. This is not a Clippy
  pass or proof that all later target diagnostics are absent.

## Fixture correction and test integrity

The first default SV2 functional rerun failed: the pinned SRI server custom-job
API stales old jobs when ntime changes on the same parent. Previously the test
could pass when the timestamps happened to match. The fixture now keeps bounded
per-job SRI validators with the original timestamp, target and extranonce;
external IDs map to each validator's internal ID. A true parent/nBits transition
retires old validators, history remains capped at 16, and duplicate hashes are
tracked across jobs. Neither PoW validation nor old-share assertions are bypassed.
The test advances mock time explicitly before the mempool refresh, preserving
and strengthening the original delayed-share/block acceptance assertions.
No SRI dependency source is modified. This fixture behavior does not certify
that another pool's SRI integration handles same-parent refresh correctly.

The new V1 negative tests require error 20, a null subscription result and EOF.
The socket-level gateway test also checks preceding configure/authorize replies
no share rejection increment, connection count recovery and unavailable-peer limits. Encrypted Setup tests exercise legal Mining
flags 0/2, legal JDS flags 0, explicit unsupported-feature errors, incompatible
versions and fixed-version requirements. Existing wrong-authority tests remain.

## Independent implementation evidence and boundaries

An independently supplied external implementation was rebuilt without source
changes and tested in isolated loopback regtest. The gateway declared its own
nonempty template, an external V1 miner solved a network block, the pool
acknowledged the submission, the local node accepted the block and a second
node received it. Its ten JDS tests also pass. This is a limited independent
interoperability result, not a production or all-pools compatibility claim.

Operator-owned source, patches, configuration, scripts, binaries and raw logs
are retained outside this repository, including exact local provenance. Do not
copy or vendor them into commits. Only generic implementation/tests and this
project's acceptance conclusions belong here.

Native Windows MSVC/Qt and Windows release execution were not available; hosted
CI was not run. Independent pool stress, full one-sided fault/accounting and
CKPool's updated Core IPC bindings remain separate gates in TASK-035/TASK-037.
PR #44 remains Draft; no merge, production endpoint or miner migration occurred.

## Reproduction and local records

Use Rust 1.96.0 with cargo test --locked --release --features test-pool; explicitly
build sv2_test_pool before functional tests. Run feature_sv2.py unchanged for the
empty-tag/16-byte baseline, then with --extranonce-size=12 --coinbase-tags and
--extranonce-size=32 --coinbase-tags. Unix CI runs all three cases.

Owned build/test records are outside the checkout: /private/tmp/task040-*,
/private/tmp/task041-*, through /private/tmp/task046-*, plus the corresponding
Linux task046-* records under /private/tmp/purity-sv2-linux/. They are local
results, not hosted CI successes. Initial failures are retained alongside reruns.
