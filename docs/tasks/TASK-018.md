# TASK-018: CoinStatsIndex cumulative overflow backport

Status: complete (local validation; not deployed)

Implement input/requirements.md's Core #30469 selective backport. Preserve
Purity Index APIs, consensus, earlier backports and disabled-node operation.

Acceptance: overflow-safe three counters and wide rewards; exact upstream
serialization in a separate directory; legacy coexistence; ordinary RPC parity
and explicit extreme-value error; append/rewind/reload correctness; no disabled
full/pruned-node migration.

Verification: coinstatsindex_tests (including >INT64_MAX and >128-bit fixtures),
feature_coinstatsindex, feature_coinstatsindex_compatibility with an actual
pre-backport Purity executable, feature_coinstatsindex_purity, feature_index_prune,
feature_init and relevant UTXO RPC/unit tests; git diff --check.

See [migration analysis](../migration-report.md),
[architecture](../architecture/coinstatsindex.md) and
[RPC contract](../api/coinstatsindex.md).

Results: four CoinStatsIndex cases and 27 related unit cases passed. All listed
functional checks passed, including both RPC transport variants and actual
legacy upgrade/downgrade. Also ran rpc_dumptxoutset and git diff --check. See
[the verification report](../../doc/coinstatsindex-backport.md) for commands,
fixture corrections, build workaround, compatibility evidence and limits.
