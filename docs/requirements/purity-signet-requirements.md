# Effective implementation amendments (2026-10-14)

These owner-approved amendments supersede conflicting statements in the v1.0
baseline below.

1. The existing `signet` chain is the Purity consensus chain. Keep
   `ChainType::SIGNET`, `-signet` / `-chain=signet`, `[signet]` and RPC
   `chain=signet`. No new chain type, Purity mode or runtime mode switch.
2. Bitcoin Signet is an external history/reference network. This client does
   not retain an option to follow its post-H continuation. Preserve every
   original pre-H block and its validation rules.
3. Use a bare 2-of-3 challenge. The owner fixed H=318621 on 2026-10-15.
   The separate H=11 fixture remains only for fast regression tests.
4. The owner subsequently requested chain parameters in a configuration file
   with simulated initial values. Implement `-signetparams=<JSON file>`, provide
   an explicitly labelled simulation example, and never label those values as
   official. Mainnet does not read this file. Consensus configuration is fixed
   for a process and must not be changed while reusing incompatible state.
5. Simulated configuration enables local implementation and acceptance tests.
   H is selected; boundary/anchor hashes, public keys, activation-block pin
   and seeds still require real evidence before public release. Public config
   contains public keys only; signing private keys live in separate wallets.
6. Latest owner amendment (2026-10-15): use `signet/`, with default P2P/RPC
   ports 38333/38332 matching Bitcoin Signet. Concurrent reference nodes use
   separate base datadirs and explicit port overrides. Earlier puritysignet/
   and 58333/58332 suggestions are superseded. No replay protection or Mainnet
   changes.

The baseline D/FR/AT identifiers below remain the acceptance index, subject to
these amendments. AT-15 now protects Mainnet/Regtest/other networks and pre-H
Signet history, not the removed original/custom Signet runtime modes. AT-17
uses external Bitcoin Core Signet and this client's Signet concurrently.


---

# Bitcoin Purity Signet hard-fork requirements (implementation-plan baseline)

**Status:** Requirements baseline v1.0, pending Codex source audit and design

**Target repository:** `saltduck/bitcoinpurity` (use the actual execution-time worktree, branch and commit)

**Deliverable:** **Implementation plan only; no implementation, repository changes or PR**

**Reference specifications:** BIP325 (Signet), current Purity Mainnet consensus specification and implementation, BIP110/RDTS

The baseline below is retained in English. Conflicting baseline statements are
superseded by the owner-approved amendments above and subsequent implementation
authorization; the original identifiers remain the acceptance index.

## 1. Background and objectives

Establish official **Purity Signet** by hard-forking the existing official
Bitcoin Signet chain at fixed height `H`, sharing all blocks, transactions and
UTXO state at heights `0…H−1`. From `H`, enforce Purity's permanent RDTS
(BIP110), the same ASERT DAA as Purity Mainnet, and **Signet signing authority
independently controlled by Purity**.

For historical synchronization and a topology similar to Purity Mainnet,
**always reuse Bitcoin Signet's original P2P magic**. Select the signing
challenge by height; **do not force the signing challenge and P2P magic to
change together**.

The highest-priority constraint is: **do not change any existing Bitcoin Purity
Mainnet consensus acceptance result, DAA result or consensus parameter**.
The original Bitcoin Signet mode must also remain available and backward
compatible under this baseline, subject to the superseding amendments above.

## 2. Confirmed decisions requiring explicit approval to change

| ID | Mandatory requirement |
| --- | --- |
| D-01 | Inherit the real official Bitcoin Signet history. Every block hash at `0…H−1` must match; do not mine replacement history from genesis. |
| D-02 | Below H, verify signatures with the original Bitcoin Signet challenge. At H and later, use the new official Purity challenge so Purity independently controls subsequent block production. |
| D-03 | Keep Bitcoin Signet's original P2P message start/magic throughout; changing the challenge at H must not change magic. |
| D-04 | Permanently activate Purity BIP110/RDTS at H. Provide a dedicated, narrowly scoped length exception for the necessary Signet Coinbase commitment; retain original limits for other transactions and other Coinbase outputs. |
| D-05 | Preserve all Mainnet BIP110 Coinbase validation results. Do not add a cross-network blanket `IsCoinBase()` exception. |
| D-06 | From H, use the same ASERT algorithm as Purity Mainnet, with a 600-second target interval and 86,400-second half-life. Do not shorten the interval. |
| D-07 | Retain Bitcoin Signet's original powLimit to preserve pre-fork PoW. Adjust only the ASERT numeric boundary required by Purity Signet; preserve execution of Mainnet's `assert((powLimit >> 224) == 0)`. |
| D-08 | Reuse and adapt Purity Mainnet's shared-prefix download, competing-chain detection and stale-consensus peer management. Expected forks announced by normal Bitcoin Signet nodes must not be treated as malicious attacks. |
| D-09 | Freeze and audit H, the H-1 hash, ASERT anchor, official signing challenge, first Purity fork-block hash, DNS seeds and other final parameters. At this baseline stage H had not been supplied; do not invent a confirmed height or hash. |
| D-10 | First provide a detailed plan and necessary questions/blockers. Do not implement or modify files without explicit subsequent authorization. |

## 3. Network and historical compatibility

### FR-01: Complete historical inheritance

1. Inherit genesis, headers, transactions, UTXOs, Coinbase maturity, scripts and
   historical consensus validation from official Bitcoin Signet.
2. Do not enable permanent Purity RDTS, ASERT or new signing rules before H.
   Fully validate the original Signet chain, including BIP325 signatures.
3. Check the frozen H-1 block hash at startup. Startup, IBD, `-reindex` and
   `-reindex-chainstate` must not skip required validation.
4. Download pre-H history from official Bitcoin Signet peers. From H, accept
   only valid Purity Signet successors.
5. Assess whether existing Signet checkpoints, `nMinimumChainWork`,
   `defaultAssumeValid` and AssumeUTXO records can be inherited safely. Do not
   reuse records inconsistent with post-fork state.

### FR-02: Independent official network configuration

1. Design explicit network selection. First assess a new `ChainType` /
   `-chain=puritysignet`, and compare a dedicated mode under `-signet`; avoid
   inadvertently changing the original meaning of `-signet`. The effective
   amendments above supersede these baseline alternatives.
2. Share Bitcoin Signet magic; identical ports are not required by this
   baseline. Consider independent defaults for ports, datadir, RPC configuration,
   wallet storage, DNS seeds and peer preferences, subject to the amendments.
3. The official challenge must be a nontrivial script requiring Purity
   authorization; production `OP_TRUE` is prohibited.
4. Define the distinct responsibilities and public interfaces of original P2P
   magic derived from the original challenge and the post-H signing challenge.
5. Correctly distinguish original Signet network identity from the current
   height's signing challenge in network information and RPCs.

### FR-03: Height-dependent signature verification

1. Below H, use the original Bitcoin Signet challenge.
2. At H and later, use the new Purity challenge; original signatures must not
   authorize new blocks.
3. Audit the existing call from context-free `CheckBlock()` to
   `CheckSignetBlockSolution()`. Design safe height-aware validation covering
   every block-validation entry point.
4. Ensure the correct challenge in `submitblock`, `getblocktemplate`, IBD,
   reorgs, both reindex modes and `TestBlockValidity`, without bypasses.
5. Mining interfaces such as GBT must return the challenge for the height
   being mined.
6. Every valid post-H block must satisfy Bitcoin SHA256d PoW, ASERT difficulty
   and the new Signet signatures.

## 4. Purity consensus rules

### FR-04: Permanent BIP110/RDTS

1. Activate the same RDTS transaction and script constraints as Purity Mainnet
   at H; do not use a one-year temporary BIP9 deployment.
2. Define `nPurityActivationHeight`, `nReducedDataGrandfatherHeight` and
   `nReducedDataGrandfatherFixHeight` for this chain and grandfather semantics
   for pre-H UTXOs. The default proposal places all three at H, after checking
   actual Mainnet code.
3. Preserve existing Purity rules for ordinary nonempty `scriptPubKey`, ordinary
   `OP_RETURN`, script items, Taproot annex/control depth/tapscript and related
   constraints.

### FR-05: Coinbase exception exclusive to Purity Signet

1. Existing `Consensus::CheckOutputSizes()` provides no unconditional Coinbase
   exemption; `ConnectBlock()` checks Coinbase outputs. Purity Signet therefore
   requires a separately bounded validation path.
2. Exempt only the script length needed for a valid signing solution in the
   **Coinbase Signet witness-commitment output actually selected by BIP141/BIP325**.
3. Do not exempt other `OP_RETURN` outputs, ordinary Coinbase outputs,
   non-Coinbase transactions or unrelated appended commitment bytes.
4. Define script encoding, commitment position/selection, solution structure,
   signature validity, extra fields and maximum size precisely at consensus
   level. Do not replace this with a blanket Coinbase exemption.
5. Apply the exception only to Purity Signet at H or later. Retain original
   pre-H Bitcoin Signet rules and the original Mainnet path.
6. Add negative tests for disguised commitments, extra payloads, duplicate
   candidates, incorrect signatures, huge pushes, other oversized Coinbase
   outputs and non-Coinbase OP_RETURN outputs.

### FR-06: The same ASERT algorithm as Mainnet

1. Retain original Bitcoin Signet DAA through H-1 and switch to
   `GetNextASERTWorkRequired()` at H under this baseline.
2. Fix `nPowTargetSpacing=600`, `nDAAHalfLife=86400`,
   `fPowAllowMinDifficultyBlocks=false` and `fPowNoRetargeting=false`.
   Retain original Bitcoin Signet powLimit.
3. The anchor must be a confirmed Signet block that exists before the fork.
   H-5 is the candidate, mirroring Mainnet's relative placement; justify the
   final choice with historical data and tests. Use anchor nBits and its
   parent's timestamp semantics.
4. Preserve Mainnet's integer formula, operation order and activation-block
   allowance for harder difficulty. Outside H, require exact nBits equality.
5. The existing 224-bit assertion is suitable only for Mainnet. Prove that
   Signet's 234-bit powLimit is safe through every intermediate calculation,
   left shift and target truncation. Provide a proven wide-bit check only for
   Purity Signet; 239 bits is a candidate, not an unproven final assumption.
6. Mainnet must keep the original 224-bit check, calculation algorithm and
   validity results. Add vector-by-vector regressions.
7. Miners must use each GBT response's bits. Do not bypass ASERT with fixed
   `--nbits`. Review the existing miner's 2016-block DAA scheduling assumptions
   and adapt them while retaining the 600-second target.

## 5. P2P and fork isolation

### FR-07: Two consensus networks sharing magic

1. Allow Bitcoin Signet and Purity Signet peers to handshake and exchange valid
   pre-H history. Reject Bitcoin Signet's post-H continuation under Purity rules.
2. Reuse `NODE_PURITY_ASERT`, `NODE_REDUCED_DATA`, stale-consensus management and
   activation hash pins. Service flags are unverified claims, not sufficient
   proof that a peer follows Purity consensus.
3. Distinguish expected chain divergence from malicious messages when conflicting
   H blocks or headers arrive. Do not improperly ban or discourage honest
   old-chain announcements.
4. Do not let old-chain peers permanently occupy Purity automatic outbound
   slots. Allow historical assistance before H and prefer peers supported by
   chain evidence after H.
5. Cover reorgs, lagging nodes, restart, headers-first IBD, mid-batch forks and
   malicious service-bit claims.
6. Address addrman/cache pollution, invalid-block downloads, bandwidth waste,
   eclipse risks and DoS protection arising from shared magic.
7. Freeze the first official H block's hash before public release. Define the
   startup, first signing/mining and client-publication sequence to avoid the
   circular dependency of requiring a pin before producing the first block.

### FR-08: Isolated operational state

1. Do not accidentally share block databases, chainstate, wallets, mempools,
   peer caches, indexes or RPC listeners between the two clients.
2. Allow both networks to run concurrently on one host.
3. Define network identity in blockchain/mining/GBT RPCs, P2P service flags,
   command-line options and logs.
4. Shared historical UTXOs create replay and address-confusion risks. Do not
   automatically add replay protection beyond Mainnet requirements; disclose
   risks and provide wallet/node isolation guidance in the plan.

## 6. Signing miner and operations

### FR-09: Independent block production

1. Independently generate, retain and back up Purity Signet signing keys; do
   not depend on the original Bitcoin Signet signers.
2. Propose a safe challenge script, such as single-signature or
   multisignature alternatives supporting key rotation, and assess wallet/PSBT compatibility and
   recovery. The effective amendment fixes bare 2-of-3.
3. Sign H and later blocks with the new challenge, construct the Coinbase
   commitment and solve real PoW. Do not substitute OP_TRUE or skipped signatures.
4. Support continuous mining, outage recovery, multi-node synchronization and
   necessary rotation/upgrade procedures. Never place signing secrets in public
   node configuration or source control.
5. Keep authorization distinct from SHA256d work. Signatures must not bypass PoW.

## 7. Acceptance criteria and executable-test mapping

| ID | Scenario | Expected result |
| --- | --- | --- |
| AT-01 | Import original Bitcoin Signet at 0…H−1 | Every height's hash and UTXO state match the reference node |
| AT-02 | Valid original signatures before H | Accept |
| AT-03 | Original-challenge signatures at or after H | Reject |
| AT-04 | Purity-authorized post-H block satisfying PoW | Accept |
| AT-05 | Missing, incorrect or altered post-H Purity signatures | Reject |
| AT-06 | Valid post-H Signet Coinbase commitment exceeding 83 bytes | Accept only under the Purity Signet exception |
| AT-07 | Oversized ordinary transaction or unselected Coinbase output | Reject |
| AT-08 | The same oversized Coinbase on Mainnet | Preserve existing rejection |
| AT-09 | Difficulty at H-1, H and H+1 | Original Signet DAA transitions to ASERT; values match an independent reference |
| AT-10 | 234-bit powLimit, ASERT boundaries and left-shift overflow | Safe, deterministic and consistent across platforms |
| AT-11 | Mainnet ASERT regression vectors | Match every pre-change result; retain the 224-bit check |
| AT-12 | Shared-magic handshake and prefix sync with old Bitcoin Signet nodes | Sync pre-H history without following the old post-H chain |
| AT-13 | Old-chain announcements, forged services and conflicting H headers | Correct isolation without erroneous bans or old-chain eclipsing |
| AT-14 | Restart, both reindex modes and deep reorg | No signature, anchor, block-pin or UTXO validation bypass |
| AT-15 | Original -signet, Purity Mainnet and Regtest under this baseline | Preserve compatible behavior and unchanged Mainnet consensus, subject to effective amendments |
| AT-16 | Mining RPC/PSBT/miner | Use the new challenge and actual ASERT bits with sustained 600-second target scheduling |
| AT-17 | Concurrent original Bitcoin Signet and Purity Signet clients | No datadir, wallet, listener or peer-cache collision |

## 8. Non-goals and prohibited changes

- Do not modify Mainnet BIP110, DAA, PoW, activation height/hash or other existing
  consensus results.
- Do not extend the Coinbase exception to Mainnet, ordinary Bitcoin Signet,
  ordinary transactions or unauthorized Coinbase outputs.
- Do not create an independent genesis or discard shared Bitcoin Signet history.
- Do not use different P2P magic.
- Do not shorten the 600-second interval, disable SHA256d PoW or use OP_TRUE as
  the public challenge.
- Do not automatically add replay protection, change address formats or alter
  Mainnet wallet behavior. Any proposed necessity must be listed separately
  for owner approval.
- The original baseline phase was audit and planning only, without implementation
  or submission; later implementation required explicit authorization.

## 9. Required investigation before assumptions

1. Inspect the actual current worktree, branch/commit and Mainnet consensus.
   Paths in this document are audit leads, not substitutes for source evidence.
2. Find all challenge consumers, validation entry points and height contexts.
   Determine whether caches or reindexing can bypass the new rules.
3. Verify BIP141/BIP325 byte encoding and actual BIP110 validation paths; derive
   the correct exception conditions and size bounds.
4. Check every CalculateASERT numeric boundary and prove that the Signet-specific
   check cannot cause 256-bit overflow or inconsistent results.
5. Inspect how existing Mainnet stale-outbound, activation pins and
   NODE_PURITY_ASERT mechanisms can safely support shared-magic Signet.
6. Compare a new ChainType and dedicated switch for change scope, compatibility,
   safety and configuration migration, then recommend an option subject to the
   effective owner amendments.
7. Define how to select and audit H, the H-1 reference hash and ASERT anchor.
   Do not fabricate real heights or hashes.
8. Assess signing-wallet/PSBT compatibility, mining flow and signing-key operations.
9. Explain pre-H synchronization and independent post-H mining without access
   to the original Signet signing authority.

## 10. Required implementation-plan deliverables

Read the relevant consensus, Signet, mining, P2P and test source files and
existing tests in full before providing:

1. **Source audit:** Key files/functions, current behavior and requirement gaps,
   with precise paths and line references.
2. **Architecture decisions:** Network selection, two challenges/one magic,
   height-aware signatures, Coinbase exception, safe ASERT width and peer
   isolation; alternatives and tradeoffs.
3. **Consensus safety proof:** Unaffected Mainnet paths, unchanged historical
   prefix, no signature bypass, non-abusable Signet exception and safe ASERT
   intermediate calculations.
4. **Phased tasks:** Objectives, files/interfaces, steps, dependencies, risks,
   rollback limits and completion criteria. Prefer small, auditable changes.
5. **Test plan:** Map AT-01…AT-17 to unit, functional, integration and regression
   tests, fixtures and independent reference implementations.
6. **Deployment/migration:** Select/freeze H, prepare keys, produce/pin the first
   fork block, bootstrap history, operate seeds, release, observe and recover.
7. **Pending decisions:** Owner-confirmed parameters, external dependencies and
   blockers. Recommend defaults without presenting them as established facts.
8. **Final acceptance matrix:** Implementation locations and validation for
   every D/FR/AT requirement.

**Strict baseline requirement:** Deliver design and an executable plan first.
Identify infeasible or high-risk conflicts precisely, propose alternatives and
owner decision points, and **never silently change requirements**.
