# CoinStatsIndex RPC and upgrade contract

`-coinstatsindex=0` remains the default. Full and pruned nodes with the index
disabled upgrade by replacing the executable and restarting normally. They
need no index migration, history downloads, reindex or manual maintenance.

With `-coinstatsindex=1`, upgraded nodes use `indexes/coinstatsindex/db/` and
synchronize the new optional index from genesis. Existing `indexes/coinstats/`
is preserved, never deserialized as the new format, and produces a warning.
No automatic deletion, rename, migration or global reindex occurs. Operators
may retain the old index for downgrade; a later downgrade must also satisfy
any unrelated Purity release compatibility requirements.

A full node retaining all historical blocks can rebuild the new index locally.
A pruned node missing required history cannot build a fresh index from its old
CoinStatsIndex alone. Existing BaseIndex startup rejects that case. Keep the
index disabled, or explicitly recover complete block history (the existing
error documents operator-selected -reindex and full redownload). An already
synchronized new index may continue under the existing pruning rules.

gettxoutsetinfo keeps its arguments, field names and ordinary numeric amounts.
The indexed total_unspendable_amount and block_info.unspendable are derived
from their four constituents. prevout_spent, coinbase and
new_outputs_ex_coinbase subtract wide cumulative counters before conversion
to CAmount. If any difference exceeds INT64_MAX satoshis, the indexed RPC
returns RPC_INTERNAL_ERROR (-32603), with message:
`CoinStatsIndex per-block amounts exceed the RPC amount range`. It does not
truncate or wrap the amount. Internal indexing continues to support wide
flows. This preserves the existing amount API without redesigning it.
Non-indexed hash_serialized_3/none/MuHash scans retain their behavior.
