# Bitcoin Purity consensus (short-term hard fork)

This document is the source of truth for the short-term hard fork. Code must
match it. Long-term ideas belong in [roadmap.md](roadmap.md) and are **not**
specified here as active rules.

Activation height `nPurityActivationHeight` is hardcoded on mainnet to
**961637** (block hash
`0000000000000000003ea74f4dafdda7ed4e02c4c1ccb9768e0ca4f9e1a35159`, the first
Purity consensus block). It must be **greater than** the ASERT anchor height
**961632** so the first ASERT-adjusted block has the anchor as an ancestor.
Historical Bitcoin / Knots validation is unchanged before that height so IBD
still works.

The activation block hash is **consensus-pinned**
(`hashPurityActivationBlock`): any header at height 961637 with a different
hash is invalid. This is enforced unconditionally in header validation and is
independent of the checkpoint at the same height (which `-checkpoints=0` can
disable). On startup, a block index that already contains a conflicting block
at 961637 (stored by other software before upgrading) is rejected and must be
rebuilt with `-reindex`.

Peers whose header chain announces a competing block at 961637 (typical
Core/Knots tips) are **not** disconnected for that announcement. Their shared
pre-activation headers are retained so IBD can download historical blocks from
them in parallel with Purity peers; only post-activation history must come from
the Purity chain.
Automatic outbound peers proven to follow such a competing block are demoted to
additional stale-consensus connections, subject to `-maxstaleoutbound`, so they
cannot occupy or receive eviction protection for a Purity-compatible outbound
slot. Once the active tip is at `nPurityActivationHeight-1` or higher, those
peers are no longer useful: the node stops opening automatic outbound
connections to them (requiring `NODE_REDUCED_DATA`) and disconnects any already
tolerated stale outbound peers.

**Fork baseline:** the Knots/BIP110 *enforcement* chain that rejected
non-signaling blocks at height 961632 — not the Core majority chain at the
same height.

## Unchanged

- SHA256d proof-of-work.
- P2P magic `f9beb4d9`, default port 8333.
- Address formats, transaction serialization, sighash (no replay protection).
- Block size / weight limits inherited from Bitcoin.

## 1. Permanent RDTS (BIP110 rules)

BIP110 Reduced Data rules become **always active** at
`nPurityActivationHeight` and never expire. They cannot be turned off with
`-consensusrules`, `rdts_consent_flag`, or similar.

After activation, version-bit 4 mandatory signaling is not required. The
rules are consensus, not a miner poll.

### Output size

Defined in `src/consensus/consensus.h`:

- Non-empty non-`OP_RETURN` `scriptPubKey`: at most **34** bytes
  (`MAX_OUTPUT_SCRIPT_SIZE`).
- `OP_RETURN` outputs: at most **83** bytes (`MAX_OUTPUT_DATA_SIZE`).

### Script (`SCRIPT_VERIFY_REDUCED_DATA`)

Defined in `src/script/interpreter.h` / `interpreter.cpp`:

- Script elements at most **256** bytes (`MAX_SCRIPT_ELEMENT_SIZE_REDUCED`),
  with the documented P2SH redeemScript-push exemption.
- Taproot control blocks limited to depth **7**
  (`TAPROOT_CONTROL_MAX_SIZE_REDUCED`).
- Taproot annex is invalid.
- `OP_IF` / `OP_NOTIF` forbidden in Tapscript.

Knots deployed this as a temporary BIP9 deployment
(`max_activation_height = 965664`, `active_duration = 52416`). Purity does
not wait for 965664: the enforcement chain stalled during mandatory
signaling, so transaction-level RDTS must turn on at the hard fork.

### RDTS UTXO grandfathering

Permanent RDTS uses a fixed creation-height boundary:
`nReducedDataGrandfatherHeight = nPurityActivationHeight`, **961637** on
mainnet. Inputs spending UTXOs created **below** this boundary retain the
legacy exemption from `REDUCED_DATA_MANDATORY_VERIFY_FLAGS`. UTXOs created
**at or above** it do not. Subsequent BIP9 STARTED, LOCKED_IN, ACTIVE or
expiry transitions must not move this fixed boundary. Output-size rules
remain active independently of spend-side grandfathering.

**Correction activation:** mainnet sets
`nReducedDataGrandfatherFixHeight = nPurityActivationHeight = 961637`, explicitly
selected by the user on 2026-10-06. Blocks at or above that height use the fixed
creation boundary. Earlier blocks retain historical validation. No operator
runtime switch can select a different mainnet correction height.

The previous implementation used BIP9 `StateSinceHeight()` even after permanent
activation, and the initial correction patch left its gate unset (`INT_MAX`).
Selecting 961637 changes historical consensus validation: it can reject spends
accepted by the old moving boundary, and restore the intended exemptions for
pre-Purity outputs. The user reports no obvious dependent confirmed transaction
in a candidate scan of active-chain heights 961637 through 967297. That report
is not a complete corrected-rule historical revalidation; no independently
verified tip hash/chainwork or full mainnet validation result is recorded here.
Local tests and this parameter change do not establish production rollout or
historical compatibility. See the audit procedure below.

Regtest alone supports `-testactivationheight=purity@200` and
`-testactivationheight=rdtsgrandfatherfix@200` for regression tests. Purity
activation there retains regtest's fixed difficulty; public network parameters
and mainnet ASERT behavior are unchanged.

#### Historical audit procedure

Use an archival node with block and undo data, synced to an independently
confirmed Purity tip. Save the exact height/hash/chainwork from
`bitcoin-cli getblockchaininfo`. Do not audit against an unconfirmed moving tip.
For each block from 961637 through that saved height:

1. Obtain its hash with `bitcoin-cli getblockhash HEIGHT`.
2. Record the underlying BIP9 boundary with `bitcoin-cli getdeploymentinfo HASH`:
   `deployments.reduced_data.bip9.since` describes the state for that block and
   was the exemption boundary in the former implementation.
3. Obtain all transactions and input creation heights with
   `bitcoin-cli getblock HASH 3`. Every non-coinbase input must contain
   `prevout.height`; missing/pruned undo data makes the audit incomplete.
4. Identify every input with `961637 <= prevout.height < bip9.since`.
   These are candidates for a removed exemption, not proof of invalidity.
   Evaluate the full input scripts/witnesses with the corrected mandatory
   flags, including all rules (annex, conditional opcodes, control depth,
   witness versions and script-element limits). Checking annex alone is
   insufficient. Also check pre-Purity inputs where the deployed boundary is
   below 961637, because restoring exemptions changes validation there.

For authoritative validation, use an isolated build of the selected correction
with mainnet `nReducedDataGrandfatherFixHeight = nPurityActivationHeight = 961637`.
Revalidate a separate archival data
copy with `-assumevalid=0 -reindex-chainstate -parkdeepreorg=0 -connect=0
-listen=0 -dnsseed=0`. Confirm that it validates through the exact saved tip
hash and chainwork, without invalid-block or script-validation errors. Falling
back to an earlier valid chain is an audit failure, not success. Retain the
revision, parameters, logs, candidate inputs and verified tip. Recheck that the
reference node still agrees with the saved chain. Audit only on separate data;
never overwrite the production chainstate. A finite audit establishes
compatibility only through its recorded tip, not future blocks before rollout.

If this revalidation rejects historical blocks, compatibility with the saved
chain is not established; investigate and reconcile the activation decision
before rollout. If history cannot be fully checked, the historical audit remains
incomplete even though the code parameter has been selected. See
[validation-issues.md](validation-issues.md).

## 2. Difficulty: aserti3-1d

Port of Bitcoin Cash **aserti3** (integer cubic approximation; no floating
point). Specification:
https://upgradespecs.bitcoincashnode.org/2020-11-15-asert/

Parameter change vs BCH `aserti3-2d`:

- Half-life `nDAAHalfLife` = **86400** seconds (24 hours), i.e. `aserti3-1d`.
- Ideal block time remains 600 seconds.

**Anchor** is enforcement-chain block **961632**:

- `anchor_height` = 961632
- `anchor_bits` = that block’s `nBits` (filled from the real block when known)
- `anchor_parent_time` = timestamp of **parent** of 961632 (BCH convention)

From `nPurityActivationHeight` onward, `GetNextWorkRequired` uses ASERT.
**Only at the activation-height block** is ASERT a minimum difficulty
(maximum allowed target): that header is valid if its decoded `nBits`
target is **less than or equal to** the ASERT target. Harder `nBits` are
accepted so a block mined with the legacy 2016-block DAA (current mainnet
high difficulty) can be the first Purity block when that target does not
exceed the ASERT maximum.

Compare the decoded `arith_uint256` targets, not the compact `nBits`
integers. Illegal compact encodings and targets above `powLimit` are still
rejected. `CheckProofOfWork` continues to check the block hash against the
block’s own `nBits`, so a harder header must actually meet the harder
target.

Every other height still requires an exact `nBits` match: the 2016-block
DAA before activation (Bitcoin Core behaviour), and the ASERT compact
value from height `nPurityActivationHeight + 1` onward.

Using an anchor in the past (while the enforcement chain has been behind
schedule) drops the ASERT floor at the first Purity block so production can
resume.

## 3. Deep-reorg parking (local policy, not consensus)

Uses the block-acceptance parking pattern of Bitcoin Cash Node, with Purity
thresholds and manual unparking. **Not** a consensus rule. Different nodes
may use different thresholds. A block or chain is not consensus-invalid merely
because a node parks it; parking only affects that node's chain selection.

Default local policy:

- Parking is enabled on mainnet (`-parkdeepreorg=1`) and disabled by default
  on test chains.
- The default threshold is **6** (`-parkreorgdepth=6`).
- If connecting a competing chain would rewind the active chain by **more
  than 6** blocks (`rewind > 6`), mark the competing chain **parked** and do
  not reorg automatically.
- Reorgs of 6 blocks or fewer proceed under normal most-work chain selection.

Operators may override the threshold with `-parkreorgdepth=<n>` (minimum 1).
For example, `parkreorgdepth=4` means reorg depths 1–4 may activate
automatically, while depth 5 or greater is parked for manual review. Disable
the mechanism with `-parkdeepreorg=0`.

Parking is evaluated in `ChainstateManager::AcceptBlock` from the current
active tip, incoming block index and active-chain fork point, after contextual
checks and storage but before `ReceivedBlockTransactions` can make the block
or previously unlinked descendants into chain candidates. The first competing
block after the fork is marked `BLOCK_PARKED`; its header index can be marked
even while its body is missing. The rule depends on the rewind depth, not body
delivery order, work arrival order, or the optional `pblock` optimization in
`ActivateBestChain`. Parked ancestry excludes descendant candidates.

`unparkblock` clears relevant parked flags and restores candidates before
ordinary `ActivateBestChain(nullptr)` activation. No special activation bypass
is needed, and no accumulated-work automatic unparking is implemented.
Consensus-valid parked branches are not made consensus-invalid.

Parking flags persist across restart and `-reindex-chainstate`.
`reconsiderblock` clears invalidity while retaining parking; it does not run a
new depth decision for bodies already accepted. Enabling parking later does
not retroactively classify all stored branches. `-loadblock` and full reindex
imports use the same acceptance path as P2P. However, full `-reindex` discards
the block index and chainstate, including parked flags. During an unpruned
rebuild, only genesis is initially activated while files are read, so there is
no deep rewind against an established active tip; final most-work selection
can activate a previously parked branch. Full reindex is therefore a reset of
local parking decisions, unlike restart or a chainstate-only rebuild.

`getchaintips` adds `parked: true|false`, true if the tip or any ancestor has
`BLOCK_PARKED_MASK`. Existing `status` strings retain their meaning; a parked
branch can still be `valid-headers` or `valid-fork`.

Parked chains can be reviewed with `parkblock` / `unparkblock`.
`invalidateblock` / `reconsiderblock` remain available to reject or restore.
`parkblock` rejects blocks already on the active chain; active-chain manual
parking/rewind is deferred. The rejection leaves the active chain unchanged.

- Do **not** port BCH Avalanche or automatic unparking.
- Do **not** port BCH `-maxreorgdepth` auto-finalization.

## 4. Double-spend freeze (specified, not implemented)

**Status: draft only. No code in this release.**

Bitcoin Cash DSProofs are mempool notifications only and are **not** a freeze
implementation.

Intended future consensus (to be finalized before coding):

- **When:** a reorg actually connects; an outpoint spent by txid A on the
  disconnected chain is spent by a different txid B on the new chain.
- **What:** freeze coinbase outputs of new-chain blocks that contain the
  conflicting spend; freeze the original input outpoint so it cannot be
  spent again.
- **Not while parked:** detection may log; freeze applies only if the reorg
  is accepted (unparked and connected), so the set is determined by chain
  history rather than local park state.

All nodes must compute the same freeze set. That requires a later spec
revision covering IBD, assumevalid, and pruned nodes.

## Testnets

Regtest may activate Purity rules at low height for tests. Public testnet /
signet parameters are chosen when those networks are actually used; they are
not required to match mainnet dates.
