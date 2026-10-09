# TASK-020: Core #34451 UTXO statistics race fix

Status: complete (local validation; not deployed)

Remove early current-tip pindex; guard historical sync checks; acquire cursor and its block index under cs_main; derive indexed predecessor from stats.hashBlock. Preserve #30469 accounting.

Validate coinstats_tests, coinstatsindex_tests and UTXO RPC/hash/index/pruning/AssumeUTXO functional regressions. See [verification report](../../doc/utxostats-backport.md). No deployment or database migration.

Results: stage-one build, 50 related unit cases and all 10 targeted functional variants passed. The deterministic fixture failed on the unmodified baseline and passes with #34451.
