# Validation and signed-manifest architecture

Fee defaults are expressed in sat/kvB: relay 100, incremental replacement 10,
mining 400 and wallet fallback 1000. Relay and incremental defaults are
independent; their compile-time check requires relay >= incremental. The
existing explicit incremental-fee override can still raise the relay floor.
Fee-filter rounding orders the native and Core incremental-fee defaults
before selecting a bucket floor, supporting the native default below Core's.
Block size and weight defaults are independently 3985000; the weight default
is not derived by multiplying the size by four. Mainnet assumevalid points to
the user-selected pre-fork block 961631. Minimum chain work, consensus limits,
fee-estimator buckets and block reservation are unchanged.

`DEFAULT_MAX_TIP_AGE` is 168 hours (604800 seconds). It initializes
`ChainstateManagerOpts::max_tip_age` and supplies the debug help default.
`node::ApplyArgsManOptions` retains explicit `-maxtipage` overrides.
`UpdateIBDStatus` retains its existing age comparison, other conditions
and latch behavior; only the default age changes.
`feature_minchainwork.py` pins node2 to `-maxtipage=86400` so its two-day
clock offset continues to keep it in IBD after the minimum work is reached.

`ChainstateManager::AcceptBlock` evaluates the active-chain fork depth after
contextual checks and successful block storage, immediately before
`ReceivedBlockTransactions`. It marks the first competing header index
`BLOCK_PARKED` when the rewind exceeds the configured depth (default 3),
even if that ancestor's body is missing, and dirties the index.
`TryAddBlockIndexCandidate` excludes parked ancestry and records descendant
`BLOCK_PARKED_CHILD`; `FindMostWorkChain` retains its ancestor exclusion.
`UnparkBlock` clears relevant descendants and ancestors before restoring
candidates. `ActivateBestChain` only selects/connects candidates; its `pblock`
is a disk-read optimization and it has no operator-approval mode.

Active-chain manual parking/rewind is deferred. The current `ParkBlock` guard
rejects active targets before mutation. Functional park-first permutations use
an inactive header branch, then accept its bodies while parked; no production
disconnect/rollback path is introduced. See TASK-016 for test-scope alignment.

Live receipt and external imports share `AcceptBlock`. Restart and
`-reindex-chainstate` retain block-index parking flags. A full `-reindex`
discards index/chainstate status and reconstructs from block files. In an
unpruned rebuild, the tip initially remains genesis during import, so no deep
rewind is detected and final most-work selection may choose a previously
parked branch. This is a local-policy reset, not automatic unparking of an
existing index. See [parking review](../../doc/deep-reorg-parking-review.md).

`GetReducedDataGrandfatherHeight` selects the deployed BIP9 boundary before
correction activation and the fixed consensus parameter afterwards. Mainnet's
creation boundary and correction activation are both 961637, as explicitly
selected by the user. Earlier blocks retain historical rules; generic BIP9 and
other-network parameters are unchanged. Local regressions do not establish
historical mainnet compatibility or deployment of the new validation rule.

The shared manifest validator traverses UniValue's original keys and values,
rejecting duplicate keys before the canonicalizer can collapse them. It checks
objects inside arrays as well. Consumers also validate their parsed DOM before
reading semantic fields. No dependency parser or general JSON-RPC changes.

Update download redirect approval uses the same artifact URI predicate as
manifest parsing, with an additional final-URL check before verification.

## Headers presync ASERT difficulty check

`PermittedDifficultyTransition` accepted any `nBits` from the Purity activation
height, so presync accepted `powLimit` headers costing about 2^32 hashes each.
`HeadersSyncState` now keeps, per phase, the ASERT anchor `nBits` and the
anchor-parent time: from `m_chain_start->GetAncestor(nAsertAnchorHeight)` when
the sync starts at or above the anchor, otherwise captured from the received
header at the anchor height and its predecessor. From the activation height the
required bits are `CalculateASERT` (or `CalculateSignetASERT`) of the anchor
target, the previous header's time minus the anchor-parent time, and the
previous height minus the anchor height; `CheckDifficultyBits` then applies the
same activation-height rule as full validation.

ASERT difficulty falls with timestamp age, so the check alone would still let a
peer lower difficulty with far-future timestamps. Presync and redownload
therefore reject headers more than `MAX_FUTURE_BLOCK_TIME` ahead of
`NodeClock::now()`, which full validation also rejects (`time-too-new`). With
times capped near now, keeping difficulty low requires timestamps to keep
advancing, so the cheap prefix of a fork is bounded by its real elapsed time.
Networks with `fPowAllowMinDifficultyBlocks` or `fPowNoRetargeting` keep the
previous behavior.

