# TASK-035: Platform builds, fuzzing and release verification

Linux/macOS/Windows ON/OFF daemon/Qt/IPC builds, DATUM regression, parser/FFI fuzzing and sanitizers.

See [requirements](../../input/requirements.md), [architecture](../architecture/embedded-sv2.md) and [contract](../api/sv2.md).

## Status

Local implementation and verification complete except native Windows/MSVC/Qt CI.
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
the pinned libevent build without editing vcpkg's source files. Hosted CI has
not been dispatched from this worktree; native Windows success remains pending.
Release macOS runners use currently available arm64/Intel images and their
image-selected Xcode. The new workflow passes actionlint 1.7.12. Existing
release scripts retain unrelated ShellCheck SC2086 findings and an intentionally
disabled Intel release job; no Intel release was enabled. General CI's existing
runner-output diagnostics are outside this change.
