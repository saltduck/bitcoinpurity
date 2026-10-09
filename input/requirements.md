# Embedded DATUM Solo Mining

## Standalone wallet transaction-removal correction

Backport only Bitcoin Core PR #34358's functional fix and regression scenario.
Removing a wallet transaction must erase only its own input spend associations,
preserving surviving conflicting transactions, including multiple inputs.
Keep the existing uint256 interfaces, TxSpends container, wallet locks and
database-commit callback. Preserve wallet formats, RPC behavior, consensus and
mempool policy; no migration, rescan or reindex is required. Test replacement,
confirmation, reload, non-conflicting removal and failed deletion. Existing
inconsistent runtime state is not automatically repaired.

## Goal

Embed the solo-mining and Stratum V1 functionality of DATUM Gateway in
`bitcoinpurityd`, so an external SHA256d miner, including common ASICs, can mine
through the Purity node without a separate `datum_gateway` process.

DATUM Gateway commit
`dbc3b143589842feb606a409b40cd70f67117b45` is the behavioral and source
provenance baseline. Do not silently update it.

## Scope

- Vendor only the upstream-derived C sources needed for non-pooled Stratum V1,
  GBT processing, coinbase construction, share validation, and block submit.
- Keep those sources compiled as C and expose a stable C ABI to a thin Purity
  C++ lifecycle/configuration bridge.
- Keep Stage 1 communication with the node on localhost JSON-RPC using
  `getblocktemplate` and `submitblock`.
- Add `BUILD_DATUM`, default `ON`; `-DBUILD_DATUM=OFF` must remove all DATUM
  sources and DATUM-only dependency requirements.
- Add runtime `datum`, default `0`; disabled startup must not listen, create
  DATUM threads, or require DATUM runtime settings.
- Support `mining.subscribe`, `mining.authorize`, `mining.configure`, and
  `mining.submit`, plus `mining.set_difficulty` and `mining.notify`.
- Preserve extranonce, version rolling/ASICBoost, share-target, network-target,
  coinbase, Merkle, nonce, endian, and block-construction behavior from the
  baseline.
- Support Linux, macOS arm64, and Windows x64 default builds.

## Qt Settings contract

- When `BUILD_DATUM=ON`, Bitcoin-Qt must retain every upstream/master Settings
  tab and option, and add one `DATUM` tab; `BUILD_DATUM=OFF` must not show that
  tab.
- The DATUM tab exposes the runtime gate, Stratum bind/port, dedicated UPnP
  mapping, and authentication, client limits, fixed payout address, share
  difficulty, coinbase tag, and the optional loopback RPC endpoint and
  credentials.
- Saving DATUM settings writes to the read/write config file, keeps sensitive
  credentials out of `settings.json`, and marks the node for restart for all
  settings except the share-difficulty, payout-address, and Coinbase-tag hot
  updates. Those three fields must update a running subsystem immediately.
  Existing configuration-file and command-line precedence remains
  authoritative.

## Qt main window and DATUM mining dashboard

- In wallet mode, Bitcoin-Qt uses a fixed left navigation rail. The navigation
  order is Overview, Send, Receive, Transactions, Address Book, and Mining.
  Pairing remains available from the Window menu.
- When Mining is selected, the main window follows the reference three-region
  composition: navigation rail, a compact current-wallet overview column, and
  the Mining dashboard. Other navigation entries continue to use the normal
  full-width wallet page.
- Address Book is an embedded wallet page with Sending and Receiving tabs. The
  existing standalone sending and receiving address windows remain available.
- Mining is compiled only with `BUILD_DATUM=ON`. Its dashboard shows only
  values derived from `DatumStatusSnapshot`: runtime state, estimated miner
  hashrate, current height, probability of the estimated miner hashrate finding
  one block, session share results, and block-candidate results.
- The dashboard derives aggregate miner hashrate from the change in cumulative
  accepted share difficulty over actual elapsed time. It samples once per
  minute while visible, uses up to five minutes of checkpoints, and requires at
  least one complete minute plus accepted work before publishing an estimate.
- The probability uses the GUI-derived miner hashrate and the estimated network
  hashrate implied by current difficulty and a 600-second target interval.
  Missing, zero, stopped, stale-session, or incomplete inputs render as
  unavailable rather than fabricated success or precision.
- The hashrate graph retains at most 24 hours in memory, preserves gaps between
  hidden periods and DATUM sessions, and does not persist samples across a GUI
  restart. Overview, miner, job, and diagnostic details remain available.
- Mining status is presented only in the Bitcoin-Qt main window. There is no
  standalone DATUM status window or Window-menu DATUM entry.
- The visible Mining page refreshes once per second and stops polling while
  hidden. It shows runtime/listener/authentication and actual port-mapping
  state, active and configured payout addresses and Coinbase tags, mining
  totals, the current template/job, connected worker details, and block-submit/
  last-error diagnostics. Hot configuration changes converge without restart.
- Worker names, remote IP addresses, and miner user agents are local-GUI-only.
  They must never be returned by `getdatuminfo`.
- Session share and block counters reset when DATUM starts, persist when a
  miner disconnects, and are not persisted across DATUM or node restarts.
- Session accepted difficulty follows the same lifecycle, is transported only
  through the internal DATUM status snapshot, and is not added to
  `getdatuminfo`.
- The Mining summary shows Best Share as the highest achieved difficulty among
  accepted shares in the current DATUM session. It resets on DATUM startup,
  survives miner disconnects, and remains internal to the Qt status snapshot.
- The dashboard contains no configuration, start/stop, or difficulty controls;
  those remain in Settings and the existing RPC surface.

## Runtime contract

When `datum=1`, support these settings:

| Setting | Default | Contract |
| --- | --- | --- |
| `datumlisten` | `127.0.0.1` | Public binding requires explicit operator action. |
| `datumport` | `23334` | Valid TCP port only. |
| `datumupnp` | `0` | Explicitly maps only the DATUM TCP port through UPnP, with PCP/NAT-PMP fallback, and requires UPnP build support plus a non-loopback IPv4 listen address. |
| `datumauth` | `0` | Missing Stratum credentials are fatal only when authentication is enabled. |
| `datumuser` | empty | Exact user or `user.worker` is accepted. |
| `datumpassword` | empty | Must match; never logged. |
| `datummaxclients` | `32` | Strict global connection bound. |
| `datummaxperip` | `4` | Strict per-IP connection bound. |
| `datumaddress` | empty | Required valid Purity payout address. |
| `datumdiff` | `65536` | Positive fixed initial/share difficulty; never changes consensus target. |
| `datumcoinbasetag` | `Bitcoin Purity` | Optional operator-controlled coinbase tag. |
| `datumrpcuser` | empty | Explicit localhost RPC credential, with safe `rpcuser` fallback. |
| `datumrpcpassword` | empty | Explicit localhost RPC credential, with safe `rpcpassword` fallback. |
| `datumrpcurl` | current network loopback RPC URL | Optional advanced override. |

The implementation must not derive the payout address from a Stratum username.
It must not reverse `rpcauth` or hard-code an RPC password.

The `setdatumdiff` RPC and the Qt DATUM share-difficulty field must hot-reload
the fixed Stratum share difficulty while DATUM is running. The Qt payout address
and Coinbase tag fields must also hot-reload the running coinbase template.
These values are persisted as `datumdiff`, `datumaddress`, and
`datumcoinbasetag`; when DATUM is not running they take effect on its next
start. Share difficulty must be an integer from 1 through 2147483647, must not
alter the network consensus target, and must cause every currently authorized
miner to receive a new `mining.set_difficulty` followed by a clean
`mining.notify`. Payout and Coinbase changes send a clean `mining.notify`.
Invalid values and calls while DATUM is stopped fail without changing the
active difficulty.
Restart behavior continues to use `-datumdiff` from the normal configuration
precedence chain.

## Security acceptance criteria

- When `datumauth=1`, unauthenticated clients receive no usable mining job and
  submit is rejected before expensive share validation or template work.
- UPnP mapping is opt-in through `datumupnp=1`; it never implicitly enables
  Stratum authentication. Public deployments should enable `datumauth=1` with
  a strong unique password.
- Authentication must complete within 10 seconds by default.
- A Stratum JSON line is bounded to 16 KiB.
- Failed authentication is tracked per IP and causes disconnect/cooldown.
- Share submissions are rate-limited per client/IP without penalizing normal
  ASIC operation.
- Threads, queues, input/output buffers, clients, per-IP clients, and share
  history are bounded.
- Credentials and other secrets never appear in logs or status RPC output.
- The configured payout address is validated with Purity address decoding.

## Lifecycle acceptance criteria

- Start only after chainstate permits GBT and the RPC server has left warmup.
- Configuration errors fail node startup clearly; operational failures are
  explicit and never silently disable the subsystem.
- Purity tip updates call an in-process refresh API instead of signaling the
  process; periodic refresh remains as fallback.
- Shutdown stops accepts and clients, wakes workers, joins every DATUM thread,
  and completes before RPC/HTTP teardown.

## Verification

- Build succeeds with `BUILD_DATUM=ON` and `BUILD_DATUM=OFF`.
- Default disabled startup has no port 23334 listener and no DATUM threads.
- Configuration tests cover invalid port/difficulty/limits and missing payout,
  Stratum credentials, and RPC credentials.
- Protocol/auth tests cover subscribe, configure, correct/wrong credentials,
  worker suffixes, pre-auth submit, job gating, and share submission.
- Robustness tests cover oversized/invalid JSON, auth flooding, invalid-submit
  flooding, and disconnect mid-message.
- Integration validation covers enabled startup, external-miner job delivery,
  real hashing, accepted and rejected shares, block-candidate submit, and clean
  shutdown. A physical ASIC is not required for release acceptance; an external
  SHA256d GPU miner using the production Stratum path is sufficient.

## Non-goals

- DATUM Prime, OCEAN pooled mining, accounting, payouts, Web dashboard/public
  management API, Stratum V2, vardiff, or a public coordinator. The main-window
  Qt Mining page and non-secret aggregate status RPC are explicitly in scope.
- Direct access to `CBlockTemplate`, `ChainstateManager`, or consensus internals.
- Changes to ASERT, difficulty adjustment, validation, chain selection,
  subsidy, P2P, mempool policy, address consensus, or any other consensus rule.
- Rewriting DATUM C as C++ or broad logger/network/mining refactors.

## Purity P2P discovery service

- Advertise experimental `NODE_PURITY_ASERT = (1ULL << 25)` in local
  VERSION and normal addr/addrv2 service records; RPC names it `PURITY_ASERT`.
- The bit is an unauthenticated discovery hint. The existing block hash at
  height 961637 remains authoritative and must also reject a conflicting
  chain advertised by a bit-25 peer.
- Automatic outbound selection prefers known bit-25 records in the existing
  AddrMan for the first 20 attempts. If there are no matching records, use normal
  selection immediately; after 20 ineligible draws, return to normal selection.
  Existing network diversity, services, recent-attempt and port checks apply.
- Missing bit 25 never independently causes rejection or punishment. Manual
  connections, anchors, feelers and bootstrap retain their existing behavior.
- Keep existing NODE_REDUCED_DATA/stale-peer rules. This service does not make
  all Core peers eligible after the shared pre-activation prefix is synced.
- Keep network magic, message/storage layouts, consensus, transaction relay
  and mempool policy unchanged.
- Test service naming/value, local VERSION/address advertisement, V1/V2 and
  AddrMan persistence, deterministic preference/fallback, legacy handshakes
  and advertised-bit activation-hash mismatches.

## Validation and signed-manifest corrections

- Active-chain manual parking/rewind is deferred. Retain the
  existing RPC rejection and test that it leaves chain state unchanged.
  Run park-first state-order tests on inactive branches; retain validity,
  descendant, restart and automatic deep-reorg coverage. No production change.
- Deep-reorg parking is determined in `AcceptBlock`, after contextual checks
  and before `ReceivedBlockTransactions` can propagate candidates. Use the
  active tip and fork point; park the first competing block when rewind exceeds
  `parkreorgdepth` (default 6), independently of delivery order and `pblock`.
  Persist the root status, exclude parked descendants, and retain the additive
  `getchaintips.parked` boolean. `unparkblock` restores normal activation without
  a special `ActivateBestChain` approval mode. Parking remains local policy;
  no automatic unparking or consensus changes. Cover boundaries, out-of-order
  receipt/import, restart, index rebuild and disabled policy. RDTS and manifests
  are unchanged by this parking architecture follow-up.
- Permanent RDTS uses a fixed UTXO grandfather boundary (mainnet 961637),
  distinct from temporary BIP9. The user selected mainnet correction activation
  at 961637, so blocks from Purity activation use the fixed boundary. Retain
  historical pre-activation and other-network behavior. The reported scan to
  tip 967297 is candidate-screening evidence, not complete historical consensus
  revalidation. Add mainnet parameter/helper regression coverage.
- Reject duplicate keys recursively before signed-manifest verification or use.
  Restrict GitHub update artifacts to saltduck/bitcoinpurity releases and check
  redirects. Preserve package URI policy, signatures, size/SHA256 verification
  and manual installation.
- Add failing regression tests first, then narrow fixes, unit/functional
  verification and consensus/security documentation. Replay protection is
  out of scope.

## CoinStatsIndex cumulative amount overflow backport

- Selectively backport Bitcoin Core PR #30469 (merge
  `1861030bea7f55d08173c34d6fa11a16e6eff454`) onto Purity master.
- Track cumulative spent prevouts, non-coinbase outputs and coinbase outputs
  with `arith_uint256`; use wide intermediate reward arithmetic and upstream
  DBVal serialization, deriving unspendable totals from their constituents.
- Use `indexes/coinstatsindex/db/`; warn about but never modify or deserialize
  legacy `indexes/coinstats/`. Preserve downgrade data. Rebuild only the new
  optional index using existing infrastructure and available historical blocks.
- Keep `-coinstatsindex=0` as the default. Disabled startup must not construct
  the index, inspect legacy data, create directories or start background sync.
  Full and pruned nodes with the index disabled need no migration or reindex.
- Preserve BaseIndex, current Chain/undo/BIP30 APIs, Purity consensus, validation,
  parking, earlier backports and non-indexed UTXO scans. Do not import #32694.
- Preserve ordinary gettxoutsetinfo field names and numeric amounts. Reject
  per-block flow differences exceeding CAmount rather than silently truncate.
- Cover disabled/pruned startup, genuine >INT64_MAX cumulative values, DB
  round trips/reload, reward identities, RPC parity, reorg/re-append/restart,
  legacy coexistence and post-activation permitted unspendable scripts.
- Document that a new index cannot rebuild from missing pruned history; never
  automatically download history, delete old data, migrate in place or force a
  global reindex. Do not commit or modify production data.

## UTXO statistics race fix and follow-up (Core #34451 / #34908)

- Backport the exact merged #34451 correctness fix before #34908 refactoring,
  as two independently built, tested and reviewable commits.
- Current-tip gettxoutsetinfo must not capture an early block index. Historical
  requests retain argument validation and the guarded index-sync height check.
  Indexed block_info obtains its predecessor from the returned stats.hashBlock.
- Create one cursor and resolve its best-block index under cs_main; release the
  acquired lock before scanning. The internal template ultimately constructs
  and returns optional<CCoinsStats>; retain the public signature, all three hash
  modes, record ordering, finalization, disk-size and interruption/error behavior.
- Preserve #30469 wide counters, bounded RPC conversion, index serialization,
  locations and legacy rules. Disabled/full/pruned scans require no index,
  synchronization, reindex, chainstate rebuild or migration.
- Preserve AssumeUTXO snapshot hashes and recursive cs_main callers. Do not
  change consensus, validation, ASERT, RDTS, parking, wallets, P2P or import #34521.
- Add deterministic cursor-snapshot regressions and concurrent functional scans;
  run hash, index/history/reorg/pruning and AssumeUTXO compatibility tests.
