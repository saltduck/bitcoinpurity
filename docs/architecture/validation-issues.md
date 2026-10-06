# Validation and signed-manifest architecture

`ChainstateManager::AcceptBlock` evaluates the active-chain fork depth after
contextual checks and successful block storage, immediately before
`ReceivedBlockTransactions`. It marks the first competing header index
`BLOCK_PARKED` even if that ancestor's body is missing, and dirties the index.
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
