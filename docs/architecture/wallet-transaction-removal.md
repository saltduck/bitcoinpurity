# Wallet transaction removal

Core PR [#34358](https://github.com/bitcoin/bitcoin/pull/34358), merged as
`cd1af852fa5d919d8a6dc0a67cb10f0a5652fb77`, fixes removal of conflicting
wallet transactions. Purity's TxSpends is an unordered multimap from COutPoint
to uint256. AddToSpends inserts one association per input; IsSpent and
GetConflicts examine all associations for an outpoint. Wallet loading calls
LoadToWallet and AddToSpends to reconstruct associations from stored history.

RemoveTxs must use equal_range for each input, find the association whose
transaction identifier matches the removed transaction, and erase only that
iterator. Erasing by key removes surviving conflicting spends as well.

The existing wallet lock and WalletBatch transaction boundary remain intact.
The successful commit listener performs ordered-entry removal, spend removal,
mapWallet removal, notification and MarkDirty. Failure or abort must not run
this cleanup. No new wallet fields, persistence formats or repair routines are
introduced; consensus, chain management and mempool behavior are unaffected.
