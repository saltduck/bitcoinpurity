# TASK-035: Platform builds, fuzzing and release verification

Linux/macOS/Windows ON/OFF daemon/Qt/IPC builds, DATUM regression, parser/FFI fuzzing and sanitizers.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Local Linux/macOS implementation and extracted-package checks pass. Hosted
three-platform/native Windows MSVC/Qt acceptance is blocked by the GitHub
account's Actions restriction. This task is not complete.
No production pool interoperability or deployment is claimed.

## Local evidence

| Platform | Result |
| --- | --- |
| macOS arm64, AppleClang 21, Release | SV2/DATUM ON daemon and Qt application; full Qt suite; eight mining cases; complete V1/SV2/JDS regtest chain. |
| macOS arm64, Release | Both features OFF daemon/Qt with nonexistent Rust tool paths; SV2 ON/DATUM OFF Qt and full Qt suite. |
| Linux arm64, Ubuntu 24.04, GCC 13, Release | Both features ON daemon/Qt, Qt suite, eight mining cases, complete regtest chain and DATUM functional regression. |
| Linux arm64, Release | Both features OFF daemon/Qt with nonexistent Rust tool paths; no Rust command in Ninja build rules. |
| Windows x64 GNU target, cross-built on Linux | Static daemon and CLI link successfully with Rust 1.96.0 and unchanged SRI dependencies. |
| Windows x64 API smoke under emulated Wine | Enabled gateway startup, retry-only outage status, RPC and joined shutdown pass. This does not replace native Windows/MSVC/Qt validation. |

The complete local scenario includes witness transaction recovery, distinct miner
extranonces, BIP310 rolled-version block PoW, duplicate/stale/invalid shares,
endpoint metadata, pool ACK accounting, pending shares across outages, unsupported testnet4
configuration rejection, declaration rejection,
protocol version/feature rejection, JDS timeout, reorg, restart and wrong
authority. The independent pool reconstructs the same block and a second node
receives it over P2P. No pool template or Solo fallback is available.

At this checkpoint IPC was forced OFF. TASK-036 subsequently restored the
optional build and verified Linux/macOS IPC with SV2 enabled; see its results.

## Parser and memory checks

The SV2 frame validator is used by production transport and by the fuzz target.
It checks extension, declared length, message codec and channel-message bit.

- Rust stable unit/integration cases pass, including Noise authentication,
  malformed framing, invalid FFI inputs, unavailable pool and cancellation of
  an incomplete Noise handshake.
- libFuzzer plus AddressSanitizer: 601,877 frame inputs and 399,694 seeded FFI
  configuration inputs, each in a bounded 31-second smoke run; no findings.
- Fuzz tools: nightly-2026-09-22 (rustc 1.100.0-nightly, 1303417c4),
  cargo-fuzz 0.13.2; separate committed fuzz lockfile.
- C++ AddressSanitizer and UndefinedBehaviorSanitizer pass all eight mining
  cases, including C buffer allocation/release and witness reconstruction.
  LeakSanitizer is unsupported on this macOS host.
- An adversarial JDS duplicate-index regression first demonstrated repeated
  transaction expansion; requests now reject repeated/out-of-range indices
  before building a bounded response.

## CI and release integration

The dedicated Embedded Stratum V2 workflow builds native Linux/macOS and
Windows static CRT daemon/Qt ON/OFF, runs protocol and functional acceptance,
checks DATUM and runs bounded ASan fuzzing. Release jobs install Rust 1.96.0
before configuring BUILD_SV2=ON. BUILD_SV2=OFF discovers no Rust tools.
The pool fixture is test-only and is not installed in release packages.

Windows uses a project-owned vcpkg triplet and policy environment to preserve
the pinned libevent build without editing vcpkg's source files. Hosted CI dispatch was attempted but rejected by GitHub; native Windows
success remains pending.
Release macOS runners use currently available arm64/Intel images and their
image-selected Xcode. Workflow validation uses actionlint 1.7.12. Release shell variables are quoted
to resolve SC2086. Validation excludes only the pre-existing constant-false
condition for the intentionally disabled Intel release job; it stays disabled. General CI's existing
runner-output diagnostics are outside this change.


## Hosted acceptance follow-through

Branch codex/embedded-sv2 was pushed at f5187c5fa3 after all 155 local CTest
cases passed. GitHub rejected the release workflow dispatch with HTTP 422:
Actions has been disabled for this user. The repository's Actions permission
is enabled, but no workflow run was created by the push. The owner confirmed
Actions cannot currently be restored. Native MSVC/Qt and hosted packaging
acceptance therefore remain blocked; no passing CI result is claimed.

A real local macOS deploy reproduced an unresolved
@rpath/libbrotlicommon.1.dylib in the project-owned deployment script.
The fix resolves load commands using the original library/plugin's directory
and LC_RPATH, while retaining the original install name for rewriting only the
bundled copy. No dependency source or original library is patched.

### Additional local acceptance

- New real Mach-O fixtures reproduce both @rpath and @loader_path transitive
  dependencies, verify relocated binaries execute, and assert original input
  executable/library bytes remain unchanged. Both subcases now pass.
- macOS arm64 Release: rebuilt with SV2, DATUM, UPnP, ZMQ, SQLite wallet and
  optional IPC enabled. All 155 CTests and the complete Qt suite pass, including
  IntroTests with offscreen. Full external-V1/SV2/JDS PoW/reorg/fault acceptance
  also passes with both mining backends compiled.
- A real macOS application ZIP is generated, extracted with framework symlinks,
  and passes deep/strict ad hoc signature verification. The extracted Cocoa
  application starts against isolated regtest/settings paths, solves a block,
  serves the next template and exits cleanly. Local Cocoa verification requires
  system GUI/XPC access outside the execution sandbox.
- Linux arm64: production executables are archived and extracted; extracted
  daemon/CLI version checks and the GUI's real PoW/template/shutdown case pass.
- The GUI functional test accepts an explicit artifact executable and platform.
  An explicitly missing executable fails rather than silently skipping.
- Release jobs now build/upload macOS application ZIPs in addition to CLI tar
  archives and verify extracted binaries/GUI on each platform. The dedicated
  native Windows job also exercises the actual GUI application.
- Release manifest staging selects the macOS GUI ZIP as the single canonical
  update artifact while retaining the supplementary CLI tar download. The
  selection was exercised against the actual local ZIP/tar pair. Checkout now
  precedes artifact download so workspace preparation cannot remove archives.
- Version checks use isolated temporary datadirs with settings writes disabled.
  Updated workflows pass actionlint with only the deliberate disabled-job
  condition excluded.

These are local results and prepared CI gates, not native Windows or hosted CI
results. The account-level restriction must be removed before the remaining
acceptance can run.

## Local regression after merging origin/master (2026-11-01)

- macOS arm64 and Linux arm64: full builds and all 156 CTests pass; full Qt
  tests pass. Rust locked unit/integration tests pass. SV2/DATUM-disabled
  macOS daemon and Qt build passes without a Rust build step.
- Extended macOS functional run: 378 scenarios, initially 306 passed, 70
  conditionally skipped and two failed. Both failures were fixed in project
  production code and their existing tests rerun successfully. The real
  600-second Signet mining cadence test passed.
- Database crash recovery exposed a post-IBD task writing UTXO state before
  persisting the referenced block index. It now uses ForceFlushStateToDisk.
  The existing 40-iteration recovery test passes independently on both systems,
  including simulated crashes, restarts and final UTXO hash comparison.
- CLI completion exposed an unsupported ANY result type in getsv2info help.
  Its result now documents concrete fields, with enabled-only fields optional.
  Existing completion and full SV2 tests pass on both systems.
- Additional Linux RPC binding tests exposed AI_ADDRCONFIG filtering numeric
  IPv6 loopback addresses. Numeric addresses now use AI_NUMERICHOST; all three
  existing IPv4/IPv6/non-loopback binding scenarios pass.
- Installed Python ZMQ support outside the repository and reran interface_zmq
  successfully. Other conditional gaps include BDB wallets, historical/reference
  binaries and platform-specific scenarios; skipped cases are not passes.
- ASan/UBSan: all 12 mining/cache regression cases pass (macOS leak detection
  disabled). macOS deployment regression, extracted app signature verification
  and Cocoa GUI PoW pass. Linux extracted archive GUI PoW also passes.
- No existing test cases or dependency sources were changed. Native Windows
  and hosted CI acceptance remain outstanding under the account restriction.

## Pre-release review gate (2026-11-01)

The F1–F4 review at e7cd08c6c8 supersedes any interpretation that local fixture
passes establish release readiness. Follow [TASK-037](TASK-037.md) for corrective
work and current evidence. PR #44 remains draft. Unmodified CKPool requires a
Core-compatible IPC validation interface, whose schema conflicts with the
existing Purity interface; direct socket interoperability is not established.
Native Windows and production-pool acceptance remain outstanding.
