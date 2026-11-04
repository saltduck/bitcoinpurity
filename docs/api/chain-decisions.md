# Signed Chain Decisions Contract (proposed)

Status: proposed interface; not implemented. Names and defaults may change
after the open decisions in the requirements are settled.

## Decision document

```json
{
  "domain": "bitcoinpurity-chain-decision-v1",
  "network": "main",
  "sequence": 7,
  "created_at": "2026-11-05T08:00:00Z",
  "expires_at": "2026-11-12T08:00:00Z",
  "block_height": 970123,
  "block_hash": "<64 hex>",
  "reason_url": "https://...",
  "signatures": [{"pubkey": "<33-byte hex>", "signature": "<base64>"}]
}
```

All fields are required; unknown or duplicate keys are rejected.

## Configuration

- `-chaindecisions=apply|confirm|off` (proposed default `confirm`).
- `-chaindecisionmaxrewind=<n>` (proposed default 1000): larger rewinds are
  never applied automatically.

## RPC

- `submitchaindecision "json"`: verify and store. In `apply` mode, apply when
  possible. Returns `{sequence, status}` with status `applied`, `pending`
  (block not yet available), `awaiting_confirmation`, or an error.
- `applychaindecision sequence`: apply a verified decision (`confirm` mode).
- `getchaindecisions`: last applied sequence and stored decisions with status.

## Warnings

Verified-but-unapplied, rejected (invalid target, limit exceeded) and failed
decisions appear in `warnings`, the GUI and `-alertnotify`. Applying a decision
clears the parked-chain warning when no parked branch has more work.

## Unchanged

Consensus, P2P messages (until optional phase 3), disk formats other than the
persisted last applied sequence, and `parkblock`/`unparkblock` semantics.
