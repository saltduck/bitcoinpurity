# Purity Signet simulation and configuration

The existing `signet` chain selects Purity consensus. There is one Signet
implementation and no Purity mode switch. RPC continues to report `signet`;
state, wallets, cookies and peer caches live in `signet/`.
`signet-params.json` is schema v2 preparation with no selected H;
`simulation.json` deliberately remains the separate H=11 fast-test fixture.

`simulation.json` is a labelled development configuration: H=11, anchor=6,
three public test-fixture keys, P2P/RPC ports 38333/38332 and non-production
seeds. It inherits the actual original Signet genesis and blocks 1 through 10.
`activation_hash: null` permits generating the first local fork block. A
non-simulation configuration must pin that hash. Changing the authority changes
the challenge transaction and invalidates existing signatures, including those
whose signer pair did not use the changed key. Rebuild incompatible databases
from verified raw blocks in a separate directory.

For launch, use the schema v2 preparation sample, with real authorities and
operated endpoints. Wait for full historical sync before choosing H. Then run
prepare.py with --config, --output, --node-cli (a local JSON CLI argument array)
and --ready. It pauses networking, runs verifychain(2,0) over all stored blocks
and undo data, then verifychain(4,288) for recent chainstate consistency. Full
historical contextual and script validation comes from the completed unpruned
sync with assumevalid=0; the bounded level-4 pass avoids a history-sized UTXO
overlay. It derives H=T+1 and A=H-5/parent time/bits/chainwork, saves checksummed
snapshot evidence and requests normal stop. The full disk scan can still take
time, but does not call for raising dbcache with chain height. The default
one-hour parent-age check is configurable operator policy. Wait for clean exit
and back up the preparation datadir before its first fixed-parameter restart.
H=318621 is obsolete. See the operator procedure below.

For a small isolated H=11 regression fixture (not launch), from the repository
root:

From the repository root, prepare a local node:

```sh
mkdir -p build/purity-signet-simulation
cp contrib/purity-signet/bitcoin.conf build/purity-signet-simulation/bitcoin.conf
cp contrib/purity-signet/simulation.json build/purity-signet-simulation/signet-params.json
build/bin/bitcoind -datadir="$PWD/build/purity-signet-simulation"
```

The sample permits configured historical connections. Import or synchronize the real shared
prefix before mining. The functional tests replay the original blocks; an
operator may import revalidated raw block records exported by a separately
running Bitcoin Core Signet. Do not copy its indexes, chainstate or wallets.
Historical seed endpoints in the JSON use explicit original-network ports;
when automatic networking is enabled they bootstrap address discovery only
while the local chain still needs the prefix.

The node refuses missing/invalid JSON, legacy challenge/blocktime overrides,
nonzero assumevalid and inconsistent frozen anchor/index metadata. Genesis,
magic, powLimit, 600-second spacing and 86400-second half-life are fixed.
The [schema](../../docs/api/purity-signet.md) specifies configurable fields.

## Deferred launch height

signet-params.json starts with no H; H=318621 is an obsolete rehearsal record.
The schema v2 preparing state validates original history and refuses mining.
Use the generated fixed file for node and miner after freezing. Existing
inactive old branches/headers are rechecked and excluded where incompatible;
active history must match the pins. The default ports and signet/ path match
Bitcoin Signet, so concurrent clients require separate base datadirs and port
overrides. simulation.json and miner-simulation.json remain paired H=11
fixtures. Rehearse the lifecycle with feature_signet_prepare.py; do not turn
known fixture keys or nonproduction seeds into an official file.

See [operator procedure](../../docs/purity-signet-operations.md) and
[acceptance record](../../docs/purity-signet-acceptance.md) for current launch
steps, historical test evidence and the remaining public release gates.

## Independent signing miner

`miner-simulation.json` configures a separate Python process and two named RPC
wallets. Its payout script is a public fixture address; replace it with your
simulation payout. All CLI commands are argument arrays. Their paths are
relative to the process working directory; chain_params is relative to the
miner configuration file. Distinct signer endpoints/datadirs can be used.
The sample contains no private keys or RPC passwords and uses local cookies.

Each signer wallet needs the same ordered bare `multi(2,keyA,keyB,keyC)`
descriptor, with only that signer's private key and the other public keys.
Create and import it through a protected wallet interface. Keep encrypted
wallet backups separately; do not place private descriptors in these JSON files.
Test-fixture authorities are for simulation only. Production authorities must
be generated and backed up independently.

After history synchronization and signer setup:

```sh
python3 contrib/purity-signet/miner.py --config contrib/purity-signet/miner-simulation.json --once
```

Omit `--once` for continuous operation. The miner verifies node fork metadata
and the template challenge, passes the virtual PSBT to each wallet without
finalizing early, checks that the wallets retained the block and virtual
transaction, finalizes the quorum, checks a full signed proposal, and runs
bitcoin-util with the exact GBT bits. It only changes the nonce after signing.
RPC payloads use stdin so large blocks do not exceed command-line size limits.
Changing tips discards stale work; an interrupted process starts from a fresh
template. Continuous mode retries RPC/quorum failures at the configured poll
interval. `--once` reports such failures and exits.

The miner logs timestamped status to stderr while stdout keeps one JSON result
per accepted block. Waiting logs show the next height and remaining seconds
every poll; signing logs identify each request and the completed quorum.
Proposal validation, PoW start/completion, submission and active-chain acceptance
are visible. During a long grind, elapsed PoW time is reported every
`poll_seconds` without restarting or imposing a total timeout on the solver.
Stale work, retry delays and Ctrl+C shutdown are also reported. Logs omit CLI
credentials, PSBTs, signatures and raw block/header payloads.

To append both channels to a local operator log, run:

```sh
python3 -u contrib/purity-signet/miner.py --config /absolute/path/to/miner-config.json 2>&1 | tee -a /absolute/path/to/signet-miner.log
```

Restart an already running miner to load the logging change; no node rebuild,
wallet change or chain-parameter update is needed. Keep operator logs private.

Scheduling waits for the parent timestamp plus 600 seconds using the node's
template time. There is no difficulty override or shortened consensus interval.
The functional test advances startup mocktime by 600 seconds for a fast local
rehearsal. It runs four daemons, stops/restores a signer, restores a wallet backup
on another daemon and signs with the alternate quorum. The optional
`tool_purity_signet_miner.py --real-cadence` run waits for a real interval
without mocktime; one such run passed locally. If a signer is unavailable,
select the other distinct pair and restart the miner.

## Parameter freeze and release gates

Keep bootstrap configurations labelled simulation until the first H block has
been generated and fully verified. Archive its raw bytes and hash, set
activation_hash to that hash, restart and verify the pinned chain. For an
official release, independently verify future H, every inherited header/body,
H-1 hash and chainwork, anchor hash/bits/parent hash/time, UTXO state and actual
authorities. Freeze public keys in script order, ports, and operated seeds.
The minimum chainwork must permit downloading the H-1 prefix from old peers.

The reference test exports simulation-pinned.json and first-fork-block.hex
to its temporary directory and verifies startup with the pin. It passed against
both the repository baseline and official Core 28.0. The ASERT source also
matched 79 raw targets on macOS arm64/Clang and Linux x86_64/GCC with UBSan.
Future official history, keys, complete release-platform runs and public
operations still require their own recorded evidence.

Run the registered tests with the generated build test runner. For an external
reference binary, run feature_purity_signet_reference.py with
`--reference-bitcoind=/absolute/path/to/bitcoin-core/bitcoind`; it compares all
simulated prefix hashes and the UTXO hash, runs both network directories at
once, produces real signed PoW, and rehearses the activation pin. Use an
isolated test directory. See the stage tasks and acceptance record in docs/.

See [operator procedure](../../docs/purity-signet-operations.md) and
[acceptance record](../../docs/purity-signet-acceptance.md) for freeze, recovery
and actual validation results.
