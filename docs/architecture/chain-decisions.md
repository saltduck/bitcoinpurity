# Signed Chain Decisions Architecture (proposed)

Status: design only; nothing described here is implemented. Requirements:
[input/requirements.md](../../input/requirements.md), section "Signed chain
decisions for parked splits". Contract: [chain-decisions](../api/chain-decisions.md).

## Problem

Deep-reorg parking protects synced nodes from privately mined deep reorgs but
has no automatic recovery. When two public chains diverge by more than
`parkreorgdepth` blocks, nodes on each side park the other chain and the
network stays split until operators act (audit H-2). Automatic work-based
unparking was rejected because a majority-hashrate attacker could satisfy it.

## Trust model

A threshold of independent key holders may choose among chains that each node
has already fully validated. They cannot make an invalid block acceptable,
cannot reach at or below the Purity activation height, and cannot force large
rewinds automatically. Nodes can verify only (`confirm`) or ignore (`off`).
This adds a coordinator to local chain selection; it is not consensus.

## Message and verification

1. Parse JSON; reject duplicate keys. Require `domain`
   `bitcoinpurity-chain-decision-v1` and the node's `network`.
2. Digest: SHA256 over the domain string followed by the canonical JSON with
   `signatures` removed (sorted keys, no whitespace).
3. Verify at least the threshold of valid signatures from distinct embedded
   public keys. These keys are used for nothing else.
4. Require `sequence` greater than the persisted last applied sequence and the
   current time before `expires_at`.

## Application

```
lookup block_hash
  missing           -> keep pending, request headers/blocks, retry on arrival
  failed/invalid    -> reject, warning
  height <= activation or rewind > max_rewind -> do not apply, warning
  mode == confirm   -> store as verified, warning until applychaindecision
  otherwise:
    UnparkBlock(chosen ancestry)
    ParkBlock(fork root of every conflicting branch with a parked or active tip)
    ActivateBestChain()
    persist sequence (block tree DB), UpdateParkedChainWarning(), log
```

Conflicting branches are parked, not invalidated, so a later decision with a
higher sequence can reverse a mistake by swapping park/unpark. If the chosen
chain later fails validation while connecting, normal invalidity handling wins
and the decision is reported as failed.

## Delivery

- Phase 1: `submitchaindecision` RPC and GUI import; decisions are announced
  on several public channels.
- Phase 2: periodic HTTPS fetch from multiple mirrors (not a single host).
- Phase 3 (optional): P2P relay so blocking a website does not stop delivery.

## Operations

Prepared rules, independent monitoring nodes with `-alertnotify`, independent
verification by each key holder before offline signing, multi-channel
publication with an incident report, and quarterly signet/regtest drills.
Mistakes are corrected with a newer decision.
