# Embedded DATUM Migration Report

## Node configuration default updates

TASK-027 updates wallet fallback, relay, incremental replacement, mining fee,
block-template capacity, mainnet assumevalid and DATUM share difficulty
defaults. Rebuild and restart to adopt omitted defaults; explicit settings
remain authoritative. DATUM Qt settings use the daemon's shared difficulty
constant. No data-format migration, reindex or rescan is required. Existing
fee-estimator buckets, minimum chain work and consensus rules are unchanged.
Block size is measured in bytes; block weight is measured in weight units.

## English Signet deployment guide (TASK-PS-010)

Translate the operator's external Chinese deployment instructions into
docs/purity-signet-deployment.md and retain the Chinese original outside the
repository. Align the older full-history level-4 instruction with the current
verifychain(2,0) plus verifychain(4,288) strategy. Use placeholders, suppress raw
RPC error output in private-descriptor examples, and link the guide from the
requirements, specification, architecture, API contract and indexes. This is a
documentation-only change; it performs no deployment, wallet operation, chain
migration or consensus change.

## Signet miner status logging (TASK-PS-009)

Add timestamped stderr status for waiting, signing, quorum/proposal validation,
elapsed PoW, submission, acceptance, stale work and retries. Preserve accepted
block JSON on stdout, CLI/configuration schema, GBT bits, the 600-second gate,
immutable signed fields, proposal/PoW checks and active-chain acceptance checks.
Poll a single PoW subprocess for heartbeat output without adding a total solver
timeout or restarting work. Reap it on an interrupted call. Logging setup runs
only on direct CLI execution and never logs credentials or signing payloads.
Restart the Python miner to apply this change. No node rebuild, wallet/chain
migration or operational configuration update is required. No deployment,
commit or PR is performed by this implementation task.

## Late Signet activation freeze (TASK-PS-008)

Schema v1 fixed files remain supported. New v2 examples start with four explicit
null activation fields, retain original historical validation and block mining.
The operator tool pauses P2P, scans all stored blocks and undo data with
verifychain(2,0), checks the most recent 288 blocks' chainstate consistency with
verifychain(4,288), then derives H=T+1/H-5 from the validated active state. Full
historical contextual and script validation comes from completed unpruned sync
with assumevalid=0; the bounded level-4 window prevents history-sized memory
growth. The tool records both results and actual chainwork, requests stop and
emits a fixed file. Back up the stopped preparation datadir before its first
fixed-H restart. The block index gains a Signet-only rule fingerprint; cached H+ status
is rechecked during conversion, inactive conflicts are excluded and incompatible
active/replay heads fail. Older fixed databases without the fingerprint and
active post-H state require -reindex-chainstate. A different fixed rule tuple
requires restoration or full -reindex. Adding the first hash pin remains allowed.
H=318621 is historical only. Mainnet parameters, RDTS output rules, ASERT formula
and 224-bit checking path are unchanged. No deployment, commit or PR is implied.

## Unsigned mainnet package manifest generator (TASK-024)

Add an offline Python manifest generator alongside the existing signer. The
generator now outputs only unsigned JSON at the user's request; the private-key
argument, automatic signing and OpenSSL dependency have been removed.
Archives containing package metadata at their data root need no changes.
Detect common enclosing directories around `blocks/` and `chainstate/`, as
already supported by the Qt downloader; the original ZIP need not be repacked.
For older archives
without it, publishers supply identity, actual height/hash and prune target
explicitly; LevelDB data is not parsed. ZIP SHA256 and compressed/uncompressed
sizes are computed automatically, including ZIP64 archives.

The generated JSON uses the existing unsigned catalog format. No node rebuild,
database migration, reindex, client trust-policy change or dependency-source
change is required. Existing remote signature verification and the standalone
signer remain unchanged. No signing, upload or deployment is performed by this
generator.

## Activation-block mismatch logging (issue #32)

Change only the received-header diagnostic from unconditional logging with an
ERROR prefix to validation-category DEBUG. Rebuild and restart to use the new
logging behavior; enable `-debug=validation` to inspect expected chain splits.
Consensus rejection, peer handling and the local-index startup ERROR remain
unchanged. No database format change, migration, rescan or reindex is required
for this logging change. An existing conflicting index still requires the
previously documented recovery. Regression evidence is recorded in TASK-023.

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

## Mainnet DNS seed restriction (TASK-025)

Keep `seed.bitcoinpurity.org.` and remove the other eight built-in mainnet DNS
seed entries. Other networks' DNS seeds, fixed seeds, DNS lookup logic and
explicit peer configuration remain unchanged. Rebuild and restart to use the
new list; no database migration, reindex or peers.dat deletion is required.
Previously learned addresses remain in AddrMan.

## Mainnet fixed seed replacement (TASK-026)

Replace the 1,306 compiled mainnet fixed seeds and the larger upstream source
list with the 11 endpoints requested by the user. Keep both lists consistent
and retain all other networks' seed arrays. The BIP155 fixed-seed format requires
numeric addresses: node0 resolves to 49.190.167.39 (ports 8333/8334) and node1
to 51.79.241.169 (8333), verified on 2026-10-12. Record these static mappings in
the source list, without adding runtime hostname resolution.

Rebuild and restart for the compiled replacement to take effect. No database
migration or reindex is required; existing peers.dat addresses remain, so this
change does not restrict all connections to the new fixed seeds.

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


## Purity Signet implementation

Baseline cc85cfad90dea5447ff313f3158147ebb7bf1b47. The existing Signet chain
becomes Purity consensus; original/custom Signet runtime modes are removed.
Pre-H official history remains mandatory. Mainnet is unchanged. The owner
authorized chain-parameter JSON with simulated initial values for development.
No H, public key or simulated hash is an official public-chain decision.

Use isolated signet/ state. Import/revalidate only historical raw blocks;
do not copy external Signet indexes, chainstate, wallets or peer caches. Fixed
pins, anchor and height-aware signatures must apply to restart, full reindex
and chainstate-only replay. Public release waits for real H-1/anchor evidence,
first-block raw/hash, official keys/seeds and all AT gates.

Current acceptance: macOS and Linux arm64 CTest each 150/150 and nine focused functional runs passed;
82 UBSan cases passed. The actual ASERT source produced 79 identical results
on macOS arm64/Clang and Docker Linux x86_64/GCC, preserving the original
224-bit assertion. Official Core 28.0 confirmed the complete simulated 0..10
prefix, UTXO hash, shared-magic sync and directory/port isolation. Simulation
first-block pin freeze/restart passed. Raw RPC/REST/P2P/ZMQ reads now validate
Signet with index height. Public examples passed a daemon/CLI smoke test.
Full future official history and production validation remain pending.
See purity-signet-acceptance.md for the current matrix.

The four-daemon miner rehearsal passed signer outage/resumption and restored
backup signing. A separate wall-clock run passed one 600-second interval.
Xcode lacks the libFuzzer runtime; later Linux libFuzzer validation is recorded
in purity-signet-review.md. See
purity-signet-operations.md for the freeze and recovery procedure.

Linux arm64 full-node functional validation passed all five selected cases
with container-local data, including signer outage and restored backup signing.

Historical amendment and audit (superseded for H): H=318621, default P2P/RPC
38333/38332, standard signet/ directory. Original per-version test-directory
mapping and wallet directory-scan additions were removed. The complete-field
JSON regression exposed and fixed omitted activation_hash acceptance.
See purity-signet-review.md for Mainnet isolation proof, actual boundary-block
records, Linux fuzz results and remaining full-history/production gates.

Documentation language: the retained Signet v1.0 baseline and source-review
record were translated into English. All documents maintained for this change
use English; requirement IDs, consensus parameters and validation evidence were
preserved. No implementation or data migration accompanies this editorial change.

## Automatic Signet signer failover (TASK-PS-011)

The standalone Python miner previously required exactly two endpoints and
aborted an attempt on either RPC failure. Keep miner schema version 1 and
two-entry compatibility; three distinct signer_clis enable ordered pair failover.
Each pair starts from the original PSBT and requires complete finalization.
Failed or invalid endpoints are skipped only for the current attempt. Signer
CLI waits have an optional positive integer timeout, default 30 seconds.
Immutable fields, signed proposal validation, PoW, scheduling, stale-tip checks
and payload-free status logging remain enforced. Coordinator failures are not
treated as signer outages.

Operators must install the updated miner, add the third protected wallet RPC
endpoint/tunnel and restart that miner once. Subsequent single-endpoint outages
need no restart. Do not change public keys, chain parameters, node databases or
wallet custody. No node rebuild, reindex or consensus migration is required.
Existing two-entry files keep fixed-pair behavior. Local regression evidence
and remaining checks are recorded in TASK-PS-011; no deployment is part of this
change.
