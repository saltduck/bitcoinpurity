# Purity Signet rehearsal and operator procedure

For the detailed English translation of the operator's Chinese setup,
signer preflight and launch instructions, see the
[deployment guide](purity-signet-deployment.md).

The current examples are simulations. Existing `signet` means Purity consensus;
public operation requires an independently audited parameter file. Keep Bitcoin
Core reference nodes and Purity nodes in separate data directories, with separate
ports, cookies, wallets and caches. There is no original-Signet runtime mode in
this client. Never import an old node's indexes or chainstate as a shortcut.

## Prepare before choosing H

H=318621 is obsolete rehearsal data. Do not put it into a launch configuration.
Use the schema v2 signet-params.json sample, replace the known fixture public
keys with the three real ordered compressed authorities and replace simulated
endpoints with operated peers. Leave activation_height, parent_hash, anchor and
activation_hash explicitly null together. Keep signing private material in
separate wallets. Ports default to 38333/38332, state to signet/ under an
independent Purity base datadir. Original reference nodes are optional additional
checks; the Purity preparing node itself fully validates original history.

```sh
mkdir -p /path/to/purity-signet
cp contrib/purity-signet/bitcoin.conf /path/to/purity-signet/bitcoin.conf
cp contrib/purity-signet/signet-params.json /path/to/purity-signet/signet-params.json
# Edit public authorities and endpoints before starting; no private keys here.
build/bin/bitcoind -datadir=/path/to/purity-signet
build/bin/bitcoin-cli -datadir=/path/to/purity-signet getblockchaininfo
```

Wait for initialblockdownload=false and headers=blocks, with
signet_fork.state=preparing. The next challenge remains the historical one;
mining RPCs and GBT proposals/templates are disabled. Keep assumevalid=0, avoid
pruning/AssumeUTXO, prepare independent signing wallets and actual Purity peers.
No early H deadline is required.

## Freeze and restart immediately before mining

Stop other import/RPC writers and keep miners stopped. After keys, signers and
network endpoints are ready, use a local cookie-authenticated CLI argument array:

```sh
python3 contrib/purity-signet/prepare.py \
  --config /path/to/purity-signet/signet-params.json \
  --output /path/to/purity-signet/fixed-params.json \
  --node-cli '["/absolute/path/to/bitcoin-cli","-datadir=/path/to/purity-signet","-rpcclienttimeout=0"]' \
  --ready
```

The tool pauses P2P, runs verifychain(2,0) over all stored blocks and undo data,
then runs verifychain(4,288) to check coin-state disconnect/reconnect consistency
over the most recent 288 blocks. Full historical contextual and script
validation comes from completed, unpruned synchronization with assumevalid=0;
the preparation configuration and startup checks enforce that policy. The
bounded level-4 pass avoids accumulating a UTXO overlay proportional to chain
history. The full level-2 disk scan can still take time, but does not require
increasing -dbcache as the chain grows. The tool derives H=T+1 and A=H-5 from
the fully validated active tip. It records exact T/A/A-1 headers, anchor
bits/parent time, T chainwork, both verification results, configuration
checksums and node state in fixed-params.json.snapshot.json. Default
--max-parent-age=3600 rejects stale parents after verification; an explicit
override changes operator policy, never consensus or block time/bits. Failed
checks after pausing leave P2P paused; inspect the failure and use
setnetworkactive true to resume preparation if safe. Existing output/evidence
files are never overwritten. The example disables the CLI time limit for the
full disk scan.

On success, stop is requested and the fixed file has simulation=true and
activation_hash=null. Wait for the process to finish cleanly. Back up the entire
stopped preparation datadir outside its original location. Retain the preparation
JSON with that backup; the next restart writes a fixed rule fingerprint to the
index. Start with the generated file:

```sh
build/bin/bitcoind -datadir=/path/to/purity-signet \
  -signetparams=/path/to/purity-signet/fixed-params.json
build/bin/bitcoin-cli -datadir=/path/to/purity-signet \
  -signetparams=/path/to/purity-signet/fixed-params.json \
  getblocktemplate '{"rules":["segwit","signet"]}'
```

Check height=H, previousblockhash=T, ordered 2-of-3 challenge and template bits.
The same datadir is supported; conflicting inactive old branches are excluded,
not adopted. Incompatible active pins/replay heads fail startup. The index never
trusts cached post-H difficulty/signature/script validity from preparation.
A legacy fixed index without the new fingerprint and active H+ requires explicit
-reindex-chainstate. Changing a fixed H/parent/anchor/authority in an existing
index fails; restore a stopped preparation backup or rebuild explicitly.

## First block and official pin

Use the standalone miner's --once option with two independent signing wallets.
Verify accepted H, its raw block, PoW, signatures, required difficulty,
transactions, witness commitment and UTXO results. Archive those raw bytes and
hash together with the snapshot and exact provisional configuration.

Create a new official JSON with activation_hash set to that actual H hash and
simulation=false. Do not alter H, parent, keys/order or anchor. Stop and restart
with it, verify the pin, rehearse both reindex modes and fresh-node sync using
the same file, and archive its SHA256. Only then enable continuous miners and
public Purity peers. Known fixture authorities and .invalid/loopback endpoints
must not be published as official. Simulation remains a launch safeguard, not
a substitute for real custody and evidence.

If launch is delayed before H is mined, keep miners stopped, restore the stopped
preparation backup into an independent datadir, resume synchronization, select a
new T and generate a new file. Do not change H online or casually repoint the
already frozen database. The default age check catches staleness only at freeze;
recheck it manually if restart or signing is delayed. ASERT cannot hide a long
gap by miner scheduling, and this procedure never alters its formula.

## Signer outage and backup recovery

- Each signer imports the same ordered bare multi descriptor containing only
  its own private key plus the other public keys. The mining process sends the
  immutable virtual PSBT to two selected RPC endpoints and carries no key itself.
  Use local cookies or an independently secured RPC channel.
- Encrypt signing wallets, keep durable encrypted backups under separate
  custody, and test restoration on an isolated machine. Verify public keys and
  a fresh virtual signature after restoration. Unlock wallets through a protected
  wallet interface; never place passphrases in the public examples.
- Continuous mining retries a failed signer at poll_seconds without submitting
  an unsigned block. Restore the same signer and it can resume. If that signer
  is unavailable for longer, choose the other distinct pair in signer_clis and
  restart the miner; configuration is loaded once per process.
- Recover an existing authority by restoring its backup on another daemon and
  changing the corresponding CLI endpoint/wallet name. Public consensus keys,
  their order, H and all pins remain identical. The functional test exercises
  this restoration and signs an accepted block with the recovered authority.
- If fewer than two authorities can be recovered, stop production and arrange
  an explicitly reviewed consensus upgrade. Do not replace public_keys in the
  existing H configuration: doing so invalidates historical signatures. Any
  actual public-key rotation needs a new activation height and preservation of
  the previous challenge history; it is a future consensus change, not a seed
  or wallet configuration operation.

## Startup, cadence and peer checks

Use getblockchaininfo, getmininginfo and GBT to inspect chain=signet, the next
challenge height, exact bits and the frozen anchor. The miner waits for the
parent timestamp plus 600 seconds, signs once, changes only the nonce during
grinding, rechecks the tip and submits real work. Restart discards stale work and
starts from a fresh template. A missing quorum, rejected proposal or failed
solver does not authorize a partial block.

getpeerinfo.signet_chain_verified records full post-fork block evidence; service
bits alone do not establish it. Original peers can bootstrap the common prefix.
After H-1, bounded stale/unverified outbound management seeks compatible peers.
Keep independently operated seeds and multiple network groups; the shared magic
does not remove ordinary eclipse threats. An unmodified original Core may reject
Purity announcements after the split, so original history connectivity cannot
substitute for operated Purity peers.

Observe block height/hash/time, peer qualification, RPC/template errors and
signer availability. Confirm actual elapsed cadence separately from tests that
advance mocktime. Before public activation a private rehearsal can be discarded;
after H do not roll back by following the old authority. Stop miners or restore
the same validated state/keys while a reviewed repair is prepared.

Miner stderr now shows timestamped waiting countdowns, signer requests and
quorum checks, proposal validation, periodic elapsed PoW, submission, acceptance,
stale work and retry delays. Stdout remains accepted-block JSON. The PoW heartbeat
uses poll_seconds and does not impose a total computation timeout. Logging does
not expose signing payloads or RPC credentials. Restart the Python miner to load
this change; leave the node, wallets and frozen consensus parameters intact.

Current executable rehearsals are feature_purity_signet, p2p_purity_signet,
tool_purity_signet_miner and the optional original-Core reference test. The miner
test's --real-cadence run uses wall-clock time; its normal run verifies outage,
alternate quorum, backup restoration and multi-node synchronization quickly.

Both clients now default to signet/ and ports 38333/38332. Use distinct base
datadirs and explicit listener overrides for simultaneous operation. This
change does not copy, delete or migrate an existing database automatically.
