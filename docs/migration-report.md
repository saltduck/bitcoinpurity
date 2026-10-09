# Embedded DATUM Migration Report

## Node configuration default updates

TASK-023 updates wallet fallback, relay, incremental replacement, mining fee,
block-template capacity, mainnet assumevalid and DATUM share difficulty
defaults. Rebuild and restart to adopt omitted defaults; explicit settings
remain authoritative. DATUM Qt settings use the daemon's shared difficulty
constant. No data-format migration, reindex or rescan is required. Existing
fee-estimator buckets, minimum chain work and consensus rules are unchanged.
Block size is measured in bytes; block weight is measured in weight units.

## Default maximum tip age

Change the omitted `maxtipage` value from 86400 to 604800 seconds (7 days).
Rebuild and restart to use the new default. Existing explicit settings remain
authoritative; no persistent-format migration, rescan or reindex is needed.
Update `feature_maxtipage.py` to exercise the seven-day default boundary and
retain the old one-day behavior as an explicit override. See TASK-022.
The full functional suite exposed a one-day default assumption in
`feature_minchainwork.py`: a two-day clock offset no longer keeps node2 in
IBD. Set `-maxtipage=86400` explicitly on that node to preserve the regression
scenario without changing production behavior.

## Baseline and current state

- Purity already provides GBT, submitblock, mempool, block assembly, validation,
  P2P, and network-specific consensus difficulty.
- DATUM Gateway commit `dbc3b143589842feb606a409b40cd70f67117b45`
  is available from `https://github.com/OCEAN-xyz/datum_gateway.git` and is the
  fixed upstream baseline.
- The repository initially contains no embedded DATUM sources or DATUM build
  dependencies.

## Upstream source impact

The non-pooled mining path uses block-template parsing, coinbase construction,
JSON-RPC, Stratum, duplicate-share tracking, block submission, utilities, and
address codecs. The independent daemon entry point, API/Web implementation,
Prime protocol implementation, configuration CLI, and their tests are not
runtime entry points for the embedded subsystem.

The retained solo path directly depends on curl and jansson. It also retains
libsodium's SHA-256 API through the upstream utility implementation;
`libmicrohttpd` remains Web/API-only and is excluded. CMake, depends, vcpkg,
and `BUILD_DATUM=OFF` isolate all three DATUM-only dependencies.

## Lifecycle gap

The baseline starts process-lifetime pthreads and uses infinite loops. It has no
coordinated interrupt/join path. SIGUSR1 merely requests template refresh. The
embedded implementation must add stop flags, wakeups, listener closure, and
thread joins without changing mining algorithms.

Purity starts HTTP/RPC in warmup before loading chainstate and calls
`SetRPCWarmupFinished()` near the end of node startup. DATUM must start only
after that point. Purity currently stops HTTP/RPC early in shutdown, so DATUM
interrupt/stop hooks must precede those calls.

## Security gap

Baseline Stratum authorization accepts a username without the shared-credential
policy required here, and it can emit work before the new authentication gate.
It provides fixed buffers and global client bounds, but Stage 1 additionally
needs per-IP bounds, auth timeout/cooldown, submit throttling, strict pre-auth
gating, and explicit 16 KiB line handling.

## Portability gap

The baseline socket layer uses epoll and POSIX APIs. The embedded source now
uses a bounded `poll`/`WSAPoll` adapter, native Windows socket types, and small
POSIX/Win32 thread and time wrappers while leaving Stratum parsing and
share/mining logic intact. The DATUM C target was cross-compiled with
llvm-mingw, and the complete `bitcoind.exe` was built with GNU MinGW 13 after
qualifying the Windows depends recipes. Static curl is isolated behind a small
interface target that supplies `CURL_STATICLIB` and its Windows system-library
dependencies in link-safe order.

## Data and consensus impact

No persistent format migration is required. No consensus or mempool rule may be
changed. GBT remains the source of `bits` and target, and submitblock remains the
final validation boundary. The only operator-controlled block data are the
validated fixed payout address and bounded coinbase tag.

The embedded coinbase builder preserves upstream behavior at production heights
and emits canonical `OP_1` through `OP_16` encodings for fresh test-chain heights.
An Apple M4 hashing run exposed the upstream small-height data-push encoding as
`bad-cb-height`; after the compatibility fix, the same diff-1 Stratum flow
advanced regtest from height 0 to 1 through the node's normal `submitblock`
validation path.

## Qt main-window migration

The wallet GUI's horizontal tab toolbar is replaced by a fixed left navigation
rail without changing wallet or node models. Address Book becomes an embedded
two-tab wallet view, while Pairing and the existing standalone address windows
remain reachable from the Window menu. The Mining entry is conditional on
`BUILD_DATUM` and reuses the existing bounded status snapshot; no persistent
format, RPC, configuration, mining protocol, or consensus migration is needed.

The hashrate trend is a bounded process-local GUI queue. DATUM exposes an
internal session cumulative accepted-difficulty counter so Qt can calculate
aggregate hashrate from work deltas over a rolling five-minute window. The
counter resets at DATUM startup, survives miner disconnect, is not persisted,
and is not exposed by RPC. The GUI queue is discarded on GUI restart and uses
gaps rather than synthetic zeros across hidden periods and DATUM sessions.
The previous Window-menu DATUM status window is removed; the main-window Mining
page is the only Qt presentation of DATUM status. The summary additionally
carries an internal session maximum of achieved accepted-share difficulty for
Best Share. Mining mode reuses the current wallet's existing Overview page as a
compact middle column beside the dashboard; no wallet data is duplicated.

## Rollout

Runtime defaults off, binds loopback by default, and requires explicit secure
configuration when enabled. Stratum authentication now defaults to off for
local compatibility; `datumupnp=1` is a separate explicit UPnP opt-in and emits
a warning when used without `datumauth=1`. Release qualification requires ON/OFF builds,
disabled/enabled functional tests, protocol and abuse tests, clean shutdown,
and at least one real external SHA256d miner hashing run through production
Stratum and `submitblock` paths. Physical ASIC hardware is not required for
release acceptance; a protocol-only handshake or cross-platform build still
does not substitute for a real submitted share.

## Validation commands

Default feature build:

```bash
cmake -B build-datum -DBUILD_DATUM=ON
cmake --build build-datum -j8
```

Isolation build:

```bash
cmake -B build-no-datum -DBUILD_DATUM=OFF
cmake --build build-no-datum -j8
```

Focused runtime validation:

```bash
python3 test/functional/feature_datum.py \
  --configfile=build-datum/test/config.ini
```

Windows x64 depends build and cross-configuration:

```bash
make -C depends HOST=x86_64-w64-mingw32 \
  NO_QT=1 NO_WALLET=1 NO_ZMQ=1 NO_UPNP=1 NO_USDT=1 -j2
cmake -B build-win \
  -DCMAKE_TOOLCHAIN_FILE=depends/x86_64-w64-mingw32/toolchain.cmake \
  -DBUILD_DATUM=ON -DBUILD_GUI=OFF -DENABLE_WALLET=OFF
cmake --build build-win --target bitcoind -j2
```

## Purity peer discovery service

Add bit 25 without wire or address database migration. Legacy nodes remain
eligible under existing policies; claims never replace the activation hash.
Bounded service preference is scoped to automatic outbound selection and
preserves AddrMan sampling, manual connections, anchors and feelers.
Existing post-prefix NODE_REDUCED_DATA gating is retained.
Validation is tracked in TASK-013.

## Validation and signed-manifest corrections

Automatic parking is now decided in `AcceptBlock` before candidate propagation,
using the active-chain fork and rewind. The intermediate `DeepReorgPolicy`
activation override has been removed; manual unpark restores normal activation.
Stored parked flags and thresholds are retained; `getchaintips.parked` is additive.
No consensus rules, RDTS or manifest behavior change in this architecture follow-up.

Restart and `-reindex-chainstate` preserve parking. Full `-reindex` reconstructs
local status and may activate a previously parked branch, because unpruned
import initially has only genesis active and therefore no deep rewind. Turning
parking on after bodies were accepted does not retroactively classify branches.
The functional test covers both rebuild modes and out-of-order `-loadblock`.
Implementation and validation are tracked in TASK-015 and
`doc/deep-reorg-parking-review.md`.

Duplicate-key manifests formerly accepted are now invalid. Publishers must
produce unique decoded object keys at every depth. Update release URI paths and
redirects must obey the official namespace/host policy; archive verification
and manual installation are unchanged.

The initial RDTS correction gate retained deployed mainnet validation with an
unset activation height. TASK-017 supersedes that parameter decision below.
Regtest activation options affect no public network.

## Deferred active-chain manual parking (TASK-016)

The user deferred active-chain manual parking/rewind on 2026-10-06. Production
behavior is unchanged. Replace the unsupported positive RPC scenario with an
ancestor/tip rejection test checking unchanged chain state. Adapt B/D ordering
tests to inactive branches while preserving all four state transitions and
validity/parking assertions. Retain existing C++ and deep-reorg regressions.
The old `--scenario=manual` test selector is replaced by
`--scenario=active-park-rejected`; normal runner registration is updated.

## Mainnet RDTS correction at 961637 (TASK-017)

The user explicitly selected mainnet correction activation at Purity height
961637 on 2026-10-06. Both the creation boundary and correction activation are
now 961637. Historical validation before activation, temporary BIP9 behavior,
other-network parameters and runtime RPC contracts are unchanged. Revalidation
from activation uses the corrected rule; changing the parameter does not itself
rebuild an existing chainstate. No production data rebuild or deployment was
performed. The reported candidate scan through 967297 is not a complete
historical consensus-validation result. See `doc/purity-consensus.md` for the
isolated archival validation procedure.

## CoinStatsIndex #30469 selective backport

- Baseline master b9a1bfddca has the Core v29.4 CoinStatsIndex implementation.
  Core v30.0 contains #30469 and newer Index interfaces introduced by #32694.
- Adopt upstream three arith_uint256 counters, DBVal field order/conversions,
  derived unspendable totals, current-block-hash continuity and DB-based counter
  restoration during MuHash rewind. Retain Purity's current undo/BIP30 and
  CustomRewind APIs. Do not import BaseIndex changes or the unrelated fuzz cleanup.
- Legacy/new index databases have incompatible record widths (128/192 bytes);
  directory separation preserves legacy bytes and downgrade availability.
  Warning text refers to pre-backport Purity, not Bitcoin Core version numbers.
- The enabled index alone needs local reconstruction. Disabled full/pruned nodes
  require no migration or reindex. Missing pruned history prevents a fresh index
  rebuild; no automatic recovery/download is introduced.
- RPC adds an explicit CAmount range rejection before GetLow64. Upstream only
  assumed ordinary per-block values fit; no public fields are redesigned.
- Test and implementation evidence is recorded in TASK-018 and the
  [backport report](../doc/coinstatsindex-backport.md).

## Wallet transaction-removal backport (Core #34358)

The merged upstream diff at `cd1af852fa5d919d8a6dc0a67cb10f0a5652fb77`
changes only transaction-spend removal and its functional regression. Purity
uses uint256 transaction identifiers and lacks upstream's m_txos bookkeeping;
retain both properties. Its send RPC accepts explicit inputs in options, and
its test framework lacks assert_not_equal; adapt the test to those interfaces.
Do not import the Core 30.x backport #34283 or alter replacement policy.

No wallet or chain database format changes, wallet migration, rescan, reindex
or reindex-chainstate are needed. Rebuild and restart the node to run the new
code. The patch prevents new corruption of input spend mappings during
removal; it does not repair every previously inconsistent wallet state.
Unload/load reconstructs spend mappings from remaining stored transactions,
but this is not a guarantee of repairing unrelated or persistent corruption.

During isolated testing, explicit abort after a successful RemoveTxs call
exposed an existing empty on_abort listener: WalletBatch::TxnAbort invokes it
and throws std::bad_function_call. This backport leaves that unrelated behavior
unchanged. The regression checks commit deferral and abort after partial
deletion failure, before listener registration; database commit/erase failure
injection and a clean abort after listener registration are not covered.

## UTXO statistics race and refactoring (TASK-020 / TASK-021)

Baseline: master 789f3781a79174bbb4a9fa75efa21c1e0a58f30b, clean detached
worktree. #30469 is present in cbe4e29c3a. Neither race backport was applied.
Reviewed exact merged patches in chronological order:
#34451 merge 4169e72d9ed6320251feea821eb7c047793a50bc (two constituent patches),
#34908 merge b6d1b65062ab123248e4f209fe3f63118f03bad6 (one follow-up patch).

Production scope: kernel/coinstats.cpp and rpc/blockchain.cpp only. Retain
Knots 29.4's ApplyStats(stats, prevkey, outputs), RPC request parameter parsing,
existing index interfaces and #30469 accounting/range checks. Upstream uses
newer ApplyStats and RPC argument APIs; those unrelated changes are excluded.
The header, FinalizeHash, UTXO ordering, consensus and snapshot formats stay
unchanged. No #34521, new dependencies, index format change or migration.

Add deterministic cursor-acquisition tests before fixing production code, then
concurrent RPC/full/pruned/index tests and existing hash/AssumeUTXO/reorg suites.
Each logical stage is independently built and validated before its commit.
See doc/utxostats-backport.md for executed results and lifetime-review limits.
