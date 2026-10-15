# Purity Signet implementation acceptance

Baseline: cc85cfad90dea5447ff313f3158147ebb7bf1b47. Local host: macOS arm64.
All changes remain uncommitted. No PR, public release or deployment occurred.
The existing signet chain is the Purity consensus chain. There is no extra mode.
Latest owner amendment: all configurable public parameters use JSON, initially
labelled simulation. The fast fixture is H=11 with real blocks 0..10.
Latest owner decisions (2026-10-15): launch H=318621, default ports
38333/38332 and signet/ directory. See purity-signet-review.md for the current
full audit, actual boundary-block evidence and Linux fuzz results.

## Implemented scope and evidence

| Stage | Implementation | Local evidence | Remaining gate |
| --- | --- | --- | --- |
| S0 | Aligned input/spec/architecture/API/tasks and effective amendments | Source baseline and call-path audit | Final owner review |
| S1 | Validated Signet JSON, isolated directory/ports, fixed magic/genesis/limit | Reader negative tests, node/CLI startup, Mainnet ignores file | Official values and actual operated seeds |
| S2 | Explicit-height signatures, strict 195-byte commitment exception, contextual/disk/replay validation | Real prefix, signed H/H+1 PoW, reorg, both reindexes, exactly 195-byte valid solution and malformed encodings | Full official-height history and adversarial replay campaign |
| S3 | Signet-only 239-bit ASERT calculator | 13 integer reference vectors, 63 Mainnet-domain comparisons, boundary tests; original Mainnet bodies unchanged | Full-node execution on remaining release platforms |
| S4 | Presync pins, shared-prefix credit, bounded stale/unverified peers, full-block qualification | Forged service flags and H conflict: prefix downloads, no ban; valid peer qualifies; baseline peer synchronizes prefix | Public mixed-network/eclipsing deployment rehearsal |
| S5 | Separate two-wallet PSBT miner, exact bits, genuine PoW, nonce-only solving and 600-second scheduling | Four daemons, signer outage/resumption, restored backup and alternate quorum, verifychain(4,0); one real-time interval | Independent-host custody/backup and sustained real-time cadence |
| S6 | Configured pins/anchor fields and simulated freeze rehearsal | Official Core/current 0..10 hashes and UTXO hash equal; archived first block, pinned config restart | Actual authorities and externally audited records |
| S7 | Registered unit/functional regressions, parameter/miner examples and operator steps | macOS and Linux arm64 CTest each 150/150 passed; four new Signet functional cases plus Mainnet mining and existing service/stale regressions passed | Official release, remaining release platforms, public operations |

The optional reference test passed against both the unmodified repository
baseline binary and official Bitcoin Core 28.0. The latter came from the
[official release](https://bitcoincore.org/bin/bitcoin-core-28.0/) and matched
the pinned checksum in test/get_previous_releases.py. Reference processes
used assumevalid=0; minimumchainwork=0 only let their short fixture chain serve
headers. Both processes used separate ports/cookies and distinct base datadirs. The earlier run used different network directory
names; the current reference regression uses signet/ for both processes,
with explicit port overrides and distinct base datadirs. The original Core accepted the genuine original block
11; this client accepted its distinct independently signed PoW block 11.

## Acceptance matrix

| ID | Test/implementation | Current result |
| --- | --- | --- |
| AT-01 | feature_purity_signet_reference: all heights 0..10, hash_serialized_3, total_amount, txouts | Passed for complete simulated prefix versus official Core 28.0; full 0..318620 validation pending |
| AT-02 | feature_purity_signet and historical parser regressions | Real original blocks 1..10 accepted |
| AT-03 | signet_tests challenge-switch and wrong-authority functional proposal | Synthetic and genuine original block-11 signatures rejected at H; original Core accepts that fixture |
| AT-04 | Functional and independent wallet miner | Signed H/H+1 with real SHA256d PoW accepted |
| AT-05 | Signature binding, missing/wrong authority, public proposals | Rejected |
| AT-06 | Signet-only exception, exact valid 195-byte unit and >83-byte functional block | Passed |
| AT-07 | Other Coinbase outputs, selected-index duplicate and ordinary transaction regression | Passed |
| AT-08 | Original CheckOutputSizes and Mainnet consensus tests | Original oversized-output refusal retained |
| AT-09 | pow activation vectors, exact GBT target and H/H+1 blocks | Local boundary tests passed; full official anchor audit pending |
| AT-10 | Integer reference, wide target multiply/shift, sanitizer build | 82 macOS UBSan cases and Linux arm64 full-node tests passed; 79 raw targets identical on macOS arm64/Clang and Linux x86_64/GCC+UBSan |
| AT-11 | Mainnet-domain comparisons and source-body comparison | Both original ASERT bodies and CMainParams unchanged; 224-bit assertion unchanged |
| AT-12 | P2P fixture and separately running baseline Signet | Full simulated historical prefix synchronized with original magic, including official Core 28.0 |
| AT-13 | Forged services, conflicting pinned H, presync pins, qualification timeout | Passed locally, no ban on expected conflict |
| AT-14 | Restart, both reindexes, H-crossing disconnect/reconnect and anchor/key mismatch | Passed; raw signature/witness corruption, cached script status and cached PoW regressions also passed |
| AT-15 | CTest suite, Mainnet mining and legacy service/stale regressions | Passed on macOS and Linux arm64; removed custom/original Signet runtime modes intentionally replaced |
| AT-16 | tool_purity_signet_miner | Four-daemon signing, outage/resumption and restored-backup quorum passed; separate wall-clock run passed with 602-second timestamps and about 610-second acceptance separation |
| AT-17 | Reference functional test and chain path handling for older binaries | Concurrent isolated official Core 28.0/current processes passed; public deployment pending |

## Requirement mapping

D-01..03 and FR-01..03 map to kernel chain parameters, Signet height selectors,
header/contextual/ConnectBlock validation, indexed block reads and presync.
D-04..05 and FR-04..05 map to the three H activation boundaries and the strict
selected-output exception; the ordinary consensus output-size checker is intact.
D-06..07 and FR-06 map to pow.cpp's separate Signet entry and pinned anchor
tuple. D-08 and FR-07 map to peer qualification, prefix preservation and bounded
stale/unverified connections. FR-08 maps to base chain parameters, ports,
RPC identity and reference-version test paths. FR-09 maps to the separate
miner and wallet configuration/recovery test. D-09 maps to JSON validation and
S6/S7 release gates. The earlier plan-only D-10 was superseded by subsequent
implementation authorization; no commit/PR/release has been created.

## Discovered pre-existing test defect

The original coinstats_tests/cursor_snapshot callback called ProcessNewBlock
while ComputeUTXOStats held cs_main. The unmodified baseline binary reproduced
that assertion. A single-process suite then reported cascading fixture failures.
Only the test was repaired: advancement now queues on a worker while the cursor
is acquired, then flushes after the lock is released and before scanning. The
test checks the original snapshot against pre-advance statistics and confirms
the chain advanced. No production CoinStats/UTXO implementation changed.
The corrected suite and complete CTest run passed.

## Risks and release decisions

- Shared magic intentionally permits old-chain peers and address-cache mixing.
  Service flags remain claims; only full-block evidence earns protection.
  Historical peers are useful before H-1 and bounded thereafter. Real network
  diversity and malicious-peer load still need deployment evidence.
- Three known test-fixture public keys and .invalid/loopback seeds are explicit
  simulation values. First-block pin starts null only in simulation; freeze it
  from verified raw bytes before release and preserve all audit artifacts.
- Reusing state with another challenge invalidates bound signatures. Do not
  use configuration edits as state migration. Import raw verified history into
  a separate directory; pruned history needs an archival source for replay.
- Original/new networks share pre-H UTXOs and address formats, so transaction
  replay and wallet confusion remain possible. Operate distinct wallets and
  configs; no new replay-protection consensus was introduced.
- Current tests do not prove future official parameters, external key custody,
  public seeds, full-node operation on every platform or sustained cadence.
  These remain explicit S6/S7 gates and are not labelled complete.

## Current validation artifacts

Ignored build/ logs are local evidence, not committed release records:

- signet-release-ctest.log: 150/150 complete CTest run.
- signet-release-functional.log: nine Signet/Mainnet/service/stale runs passed.
- signet-independent-signers.log: four-daemon outage/backup/quorum rehearsal.
- signet-real-cadence.log: wall-clock interval without mocktime.
- linux-arm64/ctest.log: GCC/Linux arm64 CTest 150/150.
- linux-arm64/functional-private.log: five node/miner/Mainnet cases passed.
- signet-core28-reference.log: official Core 28.0 prefix/UTXO/pin rehearsal.
- signet-ubsan-current-tests.log: 82 related cases with halt_on_error=1.
- cross-asert/: actual-source harness, 79 matching raw outputs, GCC version,
  Linux architecture and original assertion status 134.
- signet-raw-rpc-red.log / signet-raw-witness-red.log: reproduced raw bypasses;
  release functional run proves their repair.

The downloaded block-11 fixture records its public data source and hash;
its SHA256d header hash, original PoW and original challenge were verified
in C++ and by official Core. Official production choices remain deferred as
requested while configuration examples stay labelled simulation.

The Signet fuzz target now exercises both historical and post-fork challenge
parsers. Its C++ syntax check passed. The requested macOS libFuzzer build failed because
Xcode does not contain libclang_rt.fuzzer_osx.a. The later Linux Clang
libFuzzer + ASan + UBSan campaign is recorded in purity-signet-review.md; the
macOS limitation remains separate from those Linux results.

Operator freeze, outage, backup and custody steps are recorded in
[purity-signet-operations.md](purity-signet-operations.md).

The initial Linux functional run placed wallet data on a macOS bind mount.
restorewallet hit the existing RestoreWallet empty-directory assertion after
a failed copy/load; the shared directory exposed inconsistent wallet.dat state
across host/container. The same five functional cases, including backup restore,
passed using container-local temporary storage. No production wallet logic was
changed for this storage-specific failure. The original failed logs remain in
linux-arm64/functional.log; the passing result is functional-private.log.

Current Linux fuzz: signet 1,907,931 and signet_asert 4,202,403 executions,
each 181 seconds with Clang 18.1.3 / ASan / UBSan, both exit 0. No sanitizer
errors or assertions. Complete source audit and reproducibility metadata:
[purity-signet-review.md](purity-signet-review.md) and
[purity-signet-fuzz-results.json](purity-signet-fuzz-results.json).
