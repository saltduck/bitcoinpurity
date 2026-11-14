# TASK-038: Align IPC with Bitcoin Core v32.1

Status: implementation and local Linux/macOS validation complete; external
acceptance remains open. Supersedes TASK-050 wire-number preservation and TASK-037's
proposed separate compatibility socket. Authorized 2026-11-02.
Reference: bitcoin/bitcoin v32.1, f0de87f040621468b956f01f526a43a010310501.

Implement the v32.1 Init/Mining/BlockTemplate/Rpc/Echo/common contract on the
existing IPC endpoint, with matching ordinals, defaults, types and behavior.
Provide checkBlock, submitBlock, transaction lookup, cooldown, interruptible
waitNext and structured coinbase. submitSolution @10 returns reason/debug and
rejects duplicates; @7 and Init.makeMiningOld2 return explicit upgrade errors.
Retain Purity transaction selection, consensus and in-process mining behavior.

Verify schemas before implementation, actual socketpair calls, exact invalid/
duplicate submission results, template updates and interrupted waits, then
Linux/macOS builds, CTest, Qt and SV2/DATUM regressions. Windows remains a separate
gate. Do not patch dependency source. Record skipped/blocked results explicitly.

## CKPool migration requirements

CKPool must regenerate Cap'n Proto bindings from v32.1, use Init.makeMining @3,
Mining.checkBlock @5, and BlockTemplate.submitSolution @10 with reason/debug.
It must stop invoking old @7 and run isolated Purity consensus-backed JDS and
winning-block E2E acceptance. A schema match alone does not prove interoperability.
No CKPool source changes, production operations or external messages are authorized
by this task. PR #44 stays draft.

## Implementation

All five production schemas match the fixed Core v32.1 reference. The new
`test/util/check_ipc_schema.py` pins their canonical contract, including interface
IDs, ordinals, defaults and parameter types. Core context/thread-map framing uses
the unmodified libmultiprocess bundled in that reference archive. Its immutable
archive checksum is recorded in both depends recipes and the local build helper.
The helper was tested from an empty build directory. All 85 libmultiprocess
source files in the build were byte-compared with the archive; no patch was used.

The existing socket exposes Core's Mining/BlockTemplate/Rpc contract. Structured
coinbase metadata remains immutable after solution submission; fee/sigop vectors
exclude coinbase, and missing mempool transactions round-trip as null entries.
Purity-specific assembler options, coin-age getters and snapshot convenience
methods remain in-process. HTTP and IPC RPC share execution/whitelist handling.
IPC opens after the first chainstate tip exists. Shutdown disconnects external
peers while preserving the controlling parent until normal teardown. IPC logging
uses Core-style levels; payload traces require the trace level.

## Test integrity and red/green evidence

- The schema contract check initially failed against the inherited schemas, then
  passed after replacing the production wire contract.
- Deprecated @2/@7 calls are required to return explicit upgrade errors. The
  previous libmultiprocess v5 build aborted on the worker exception; the pinned,
  unmodified Core snapshot transports it correctly.
- Missing-transaction entries exposed an empty-Data deserialization failure;
  Core's CTransaction field-presence customization fixes the actual codec path.
- A valid submitted coinbase with two payout outputs exposed two failing
  metadata assertions (reward remaining and required outputs). Keeping the
  original metadata snapshot makes both pass without relaxing the assertions.
- Existing miner/cache assertions are retained. Call-name changes select the
  in-process snapshot helpers. The old BlockValidationState codec and its full
  round-trip assertions moved to the test-only schema because it is absent from
  Core's production contract. The old Purity-only wire-option assertions are
  replaced by Core's three-field/default checks; local option defaults remain
  checked. The new fee-threshold fixture uses a standard signed P2PKH spend;
  production transaction policy is unchanged.
- New real-node lifecycle coverage holds a Unix peer open during shutdown and
  repeats after chainstate reindex. It passes with the listener enabled only
  after chainstate initialization. An initial runner invocation omitted CMake
  reconfiguration, so its generated test directory lacked the new script; both
  final runs reconfigured and executed the test successfully.

## Local validation, 2026-11-02 Australia/Melbourne

Linux Ubuntu 24.04 ARM64 and macOS ARM64, IPC/SV2/DATUM/Qt enabled, SQLite wallet,
BDB disabled, Rust 1.96.0, Cap'n Proto 1.2.0:

- Both full builds and **157/157 CTests**, including Qt and schema contract, pass.
- macOS: **10/10** selected functional scenarios pass. Linux: **13/13** pass,
  adding all three rpc_bind variants. Both include feature_sv2 (actual PoW,
  pool acknowledgement and second-node propagation), feature_datum,
  feature_ipc_gui, feature_ipc_lifecycle, mining_basic,
  mining_prioritisetransaction, rpc_misc, rpc_users, rpc_whitelist and
  rpc_invalid_address_message. These selected runs have no skips.
- **16/16** IPC/miner/cache tests pass with address+undefined sanitizers after
  rebuilding both unmodified dependencies with the same instrumentation. The
  earlier mixed release-dependency/instrumented-project run reported a Cap'n
  Proto vector access; it is not counted as a pass. No sanitizer suppression was
  added; only macOS-unsupported leak detection is disabled.
- The macOS GUI/daemon build with IPC/SV2/DATUM disabled passes with nonexistent
  Rust compiler paths. This confirms the disabled configuration needs no Rust.
- Linux local tar packaging/extraction, daemon/CLI version checks and extracted
  GUI IPC/PoW regression pass. The macOS deploy target creates the release ZIP;
  its extracted native Cocoa GUI IPC/PoW regression also passes. The package
  contains Cocoa/minimal plugins, so an initial offscreen launch failed as
  expected; Cocoa required an unsandboxed local run for WindowServer access.
  No plugin was copied into the package to make the test pass.

Reproducible checks use `cmake --build <build>`, `ctest --test-dir <build> -j6
--output-on-failure`, and `test/functional/test_runner.py` with the scenarios
above and BITCOIND pointing to the multiprocess `bitcoin-node`. Sanitizer tests
select `ipc_tests,miner_tests,miner_cached_validation_tests`. Unix CI now also
runs the schema and lifecycle regressions.

Local evidence: `/private/tmp/task038-final-{config,build,ctest,functional}.log`,
`/private/tmp/task038-final-asan.log`,
`/private/tmp/task038-helper-fresh.log`, and
`/private/tmp/purity-sv2-linux/task038-final-{config,build,ctest,functional,package}.log`.
These are local records, not GitHub CI results.

## Remaining acceptance gates

This task does not establish independent Core-client or CKPool end-to-end
interoperability. CKPool's Core v31-era bindings still need the changes above and
isolated consensus-backed acceptance. No CKPool source or production service was
changed. Native Windows MSVC/Qt, Windows release-package execution and hosted CI
have not been rerun by this task. TASK-049/TASK-037 release gates remain open;
PR #44 must stay Draft. The earlier independent network-fault and saturation
acceptance gaps are not closed by these IPC checks.

Local package SHA-256 (unpublished local artifacts):

- `Bitcoin-Purity-1.1.0-arm64-apple-darwin.zip`: `7b5a4a6f6d69ebd611b04985ae4111d47d2c87f4be90137f19a89dec241808a2`.
- `bitcoin-purity-local-aarch64-linux-gnu.tar.gz`: `e3330e4269481e342bddf9c98bbe80da5267c491527e2df0b94a461933f00e90`.

Extracted macOS runtime log: `/private/tmp/task038-final-package-runtime-cocoa-unsandboxed.log`.
