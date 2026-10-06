# Bitcoin Purity Product Specification

## Embedded solo mining

Bitcoin Purity includes an optional embedded DATUM Gateway Stage 1 subsystem
that serves external SHA256d miners, including common ASICs, over Stratum V1 and
uses the same node's localhost JSON-RPC `getblocktemplate` and `submitblock`
interfaces.

The compile-time feature `BUILD_DATUM` is enabled by default. Runtime activation
is disabled by default and requires the operator to set `datum=1`, a valid fixed
Purity payout address, authentication credentials, and usable localhost RPC
credentials when authentication is enabled. Authentication is disabled by
default; enabling the compiled feature must not alter consensus behavior or
ordinary node operation while runtime activation is disabled.

The subsystem preserves the mining, share-validation, version-rolling,
extranonce, coinbase, Merkle, endian, nonce, and block-submit behavior of DATUM
Gateway commit `dbc3b143589842feb606a409b40cd70f67117b45`. Upstream-derived
sources remain C and are isolated from Purity C++ by a thin C ABI bridge.

Release acceptance requires a real external SHA256d miner to hash production
Stratum jobs and submit a share through the normal block-validation path. A
physical ASIC is not required; an external GPU miner is an accepted verifier.
This acceptance policy does not remove the product requirement to remain
compatible with common SHA256 ASIC clients.

## Bitcoin-Qt main window and Settings

In wallet mode the main window uses a fixed left navigation rail with Overview,
Send, Receive, Transactions, Address Book, and Mining entries. Pairing moves to
the Window menu. Address Book embeds Sending and Receiving views while the
existing standalone address windows remain available. Mining mode uses the
reference three-region composition: the navigation rail, a compact overview of
the current wallet, and the Mining dashboard. Other navigation entries retain
the normal full-width wallet page.

When `BUILD_DATUM=ON`, Mining opens a read-only dashboard backed by
`DatumStatusSnapshot`. It displays runtime state, estimated miner hashrate,
current height, a difficulty-derived chance per block, session share results,
block-candidate results, and the existing miner/job diagnostics. Aggregate
hashrate is calculated from cumulative accepted share difficulty using actual
elapsed time and a rolling window of up to five minutes. Sampling begins with a
baseline, occurs once per visible minute, keeps at most 24 hours in memory, and
resets when Bitcoin-Qt restarts. Missing or incomplete inputs are shown as
unavailable. `BUILD_DATUM=OFF` builds contain no Mining entry or dashboard.

The Qt Settings dialog preserves the complete upstream/master Settings surface
(including Main, Wallet, Network, Mempool, Spam filtering, Mining, and Display)
and adds a `DATUM` tab when `BUILD_DATUM=ON`. The tab configures the embedded
runtime gate, Stratum listener, dedicated DATUM UPnP mapping, authentication,
bounded client counts, fixed Purity payout address, share difficulty, coinbase
tag, and optional loopback RPC credentials. Changes are written to the
read/write config file, credentials are not written to `settings.json`, and a
restart is required only for configuration changes other than the runtime
share-difficulty, payout-address, and Coinbase-tag updates. The Qt fields use
the same runtime update path while DATUM is running.

Mining status is presented only in the main-window Mining page; the Window menu
does not expose a separate DATUM status window. The page refreshes only while
visible and shows runtime and actual port-mapping state, session share totals,
current job/template data, connected workers, and block-submit diagnostics.
The active runtime payout address and Coinbase tag update immediately after a
hot configuration change. Worker names, IP addresses, and user agents remain
local to the GUI; the status RPC exposes aggregate data only. Session counters
survive miner disconnects but reset on the next DATUM run. The cumulative
accepted-difficulty input, GUI-derived hashrates and chance, and Best Share are
internal-only and do not extend `getdatuminfo`.

`datumupnp=1` is an explicit opt-in that maps only the configured DATUM TCP
port, independently of the node's P2P `upnp` setting. It uses miniupnpc when
an SSDP/IGD service is available and falls back to PCP/NAT-PMP, matching the
node's existing port-mapping behavior. It requires a build with UPnP support
and a non-loopback IPv4 DATUM listen address (or `0.0.0.0`). It does not
enable Stratum authentication;
public deployments should set `datumauth=1` and configure a strong unique
password.

While DATUM is running, the `setdatumdiff` RPC or the Qt share-difficulty field
can hot-reload the fixed Stratum share difficulty (1 through 2147483647), and
the Qt payout address and Coinbase tag fields can hot-reload the coinbase
template. The selected values are persisted as `datumdiff`, `datumaddress`, and
`datumcoinbasetag`; authorized miners receive a clean `mining.notify` for the
coinbase changes, while difficulty changes also send `mining.set_difficulty`.
The network consensus target is unchanged. A restart loads the persisted values
when DATUM is not running.

The public protocol, configuration defaults, security controls, lifecycle,
verification criteria, and non-goals are normative in
[`input/requirements.md`](../../input/requirements.md).

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

- Active-chain manual parking/rewind is deferred. `parkblock` rejects active
  blocks without changing chain state; park-first ordering tests use inactive
  branches. Existing automatic parking and state-independence coverage remains.
- Automatic deep-reorg parking is decided in `AcceptBlock`, before block data
  can propagate most-work candidates. The active-tip/fork rewind exceeding
  `parkreorgdepth` parks the first competing block; headers may already exist
  while its body is missing. Persisted parked ancestry excludes descendants.
  `ActivateBestChain` has no automatic parking decision or approval override;
  `unparkblock` clears flags and invokes normal activation. Delivery order and
  `pblock` do not affect parking. `getchaintips.parked`, thresholds/defaults and
  local-policy semantics are retained; no automatic unparking is implemented.
  Restart and chainstate-only rebuild retain flags. Full reindex reconstructs
  local status from scratch and may select a previously parked branch when
  there is no deep rewind against an established active tip.
- Permanent RDTS's intended grandfather boundary is fixed at activation
  (mainnet 961637). The user selected correction activation at 961637 as well;
  all mainnet blocks from that height use the fixed boundary. Earlier blocks
  retain historical validation. Candidate screening through 967297 was reported
  by the user; complete historical revalidation has not been established here.
- Signed manifests reject duplicate keys recursively before verification/use.
  GitHub release downloads are restricted to saltduck/bitcoinpurity; each
  redirect and final URI are validated. Signature, size, SHA256, package-host
  restrictions and manual installation remain enforced.
- Scope excludes replay protection. Detailed
  consensus compatibility and historical audit procedure are in
  [purity-consensus.md](../../doc/purity-consensus.md).
