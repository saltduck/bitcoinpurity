# Purity Signet rehearsal and operator procedure

The current examples are simulations. Existing `signet` means Purity consensus;
public operation requires an independently audited parameter file. Keep Bitcoin
Core reference nodes and Purity nodes in separate data directories, with separate
ports, cookies, wallets and caches. There is no original-Signet runtime mode in
this client. Never import an old node's indexes or chainstate as a shortcut.

## Parameter and first-block freeze

1. Generate three signing authorities under separate custody. Record compressed
   public keys in their challenge order; independently confirm the bare 2-of-3
   script. Keep private descriptors out of public configuration and source.
2. H is fixed at 318621 and the launch anchor is H-5=318616. Verify the
   existing H-1, anchor and anchor-parent hashes, anchor bits and
   parent time against independent original Signet nodes. Compare inherited
   history and UTXOs with assumevalid=0. Set minimum_chain_work so original peers
   can serve the H-1 prefix; do not retain a later original-chain threshold.
3. Prepare a private provisional configuration, explicitly simulation=true and
   activation_hash=null, with the actual audited public values. Synchronize the
   prefix, inspect getblockchaininfo.signet_fork and GBT height/challenge/bits.
   Genesis, message magic, powLimit, spacing and half-life remain fixed.
4. Run the independent miner with two signing services. Verify real PoW, new
   authority, exact required bits and every applicable RDTS rule. Archive the
   first block's raw bytes, hash and the parameter/UTXO verification report.
5. Set activation_hash from that verified block. Rehearse restart, both reindex
   modes and a fresh node import with the pin. Use a fresh index if a provisional
   rehearsal stored a competing H block. Pinning rejects that competing history.
6. Replace all fixture keys/seeds/ports as required, confirm actual authority
   custody and operated endpoints, then set simulation=false for the audited
   official file. Publish only after owner approval. Archive its exact bytes and
   checksum together with the first-block record. Public nodes use the frozen
   configuration and obtain that existing first block before mining H+1.

The public sample's keys have known fixture secrets and are unsuitable for an
official file. A configuration edit does not migrate incompatible state.

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

Current executable rehearsals are feature_purity_signet, p2p_purity_signet,
tool_purity_signet_miner and the optional original-Core reference test. The miner
test's --real-cadence run uses wall-clock time; its normal run verifies outage,
alternate quorum, backup restoration and multi-node synchronization quickly.

Both clients now default to signet/ and ports 38333/38332. Use distinct base
datadirs and explicit listener overrides for simultaneous operation. This
change does not copy, delete or migrate an existing database automatically.
