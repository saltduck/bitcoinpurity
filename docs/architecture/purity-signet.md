# Purity Signet architecture

Baseline: cc85cfad90dea5447ff313f3158147ebb7bf1b47.

`signet` directly selects Purity consensus; no extra mode or chain type. The
owner authorized a parameter JSON and simulated initial values after approving
the plan. Preserve original history, genesis, magic 0a03cf40 and powLimit.

- Parameter construction: validate a fixed process configuration (H, parent
  hash, anchor tuple, three compressed public keys, activation pin, chainwork,
  ports and seeds). Keep spacing 600 and half-life 86400. Mainnet is unaffected.
- Signature checks: use parent/index height, historical challenge before H,
  new challenge at/after H. Repeat in ConnectBlock and indexed disk reads.
  Context-free caches cannot cache a height-sensitive verdict. Unsigned
  template checking is internal and cannot authorize submissions.
- RDTS: all three activation/grandfather heights are H. Keep the ordinary
  CheckOutputSizes function. Only the selected canonical BIP325 Coinbase
  commitment can exceed 83 bytes, up to 195.
- DAA: use a separate Signet calculator, identical integer formula and order,
  with powLimit < 2^239. Leave the original mainnet calculators untouched.
  Only activation permits a harder target; other heights require exact bits.
- P2P: check pinned boundaries before Signet DAA classification. Keep valid
  shared headers on a mid-batch fork, use H-1 chainwork for bootstrap, bound
  stale peers, and qualify peers by pinned-chain/full-block evidence.
- Miner: independent two-wallet PSBT pipeline, exact GBT bits, immutable signed
  template apart from nonce, actual SHA256d work and 600s scheduling.
- Storage: signet/; defaults P2P/RPC 38333/38332, matching Bitcoin Signet.
  Concurrent external Core processes need separate base datadirs and port
  overrides. No AssumeUTXO in the first release; preserve replay semantics.
- Preparation: schema v2 accepts only a complete null activation group. Keep
  H at the existing inactive sentinel internally, retain historical challenge
  and Bitcoin DAA, and block assembly/mining RPCs. RPC exposes nulls and
  preparing state. No new chain type or alternative runtime chain is added.
- Freeze: prepare.py pauses P2P, requires full sync and verifychain(4,0),
  snapshots the active parent, H-5 anchor and parent time, sets minimum work to
  parent chainwork and requests shutdown. It never derives from header-only tip
  or changes time/bits. Operator age/readiness checks are not consensus.
- Conversion: block-index DB key S stores a Signet rule fingerprint. Preparing
  may become fixed once. Fixed H/parent/anchor/keys cannot change in that DB;
  filling the H hash is permitted and independently enforced; an existing pin
  cannot be removed or replaced. On conversion,
  recheck stored header constraints and full H+ data, downgrade cached script
  validity so candidate connection rechecks UTXOs/scripts. Failed branches and
  descendants are excluded. Validate active coins/replay heads before replay;
  incompatible active state requires backup restoration or explicit reindex.
  Missing legacy fingerprints with active H+ require chainstate revalidation.
  Revalidated stored indexes are synced before publishing the rule fingerprint,
  so a crash cannot publish the context while retaining stale legacy flags.
  Mainnet does not read/write this key and retains its activation-pin failure.
- Launch: H=318621 is obsolete; H=T+1 is frozen just before mining. H=11 remains
  the fast regression fixture. First-block pin/custody/seeds still require actual
  operational evidence. Frozen parameter changes require restoring a clean
  preparation backup or full reindex, not an online edit.

Stages: S0 -> S1 -> {S2,S3} -> {S4,S5} -> S6 -> S7. See the task index.
Generic implementation and local simulation have passed their recorded checks.
S6/S7 future official parameters, full future-height history, all-platform
release builds and public deployment remain gated on their actual evidence.
The acceptance record includes macOS/Linux ASERT comparisons, UBSan, official
Core 28.0 prefix/UTXO sync, adversarial disk/cache cases and parameter freeze.
