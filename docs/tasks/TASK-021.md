# TASK-021: Core #34908 UTXO statistics refactor

Status: pending local verification

After TASK-020, move cursor/stat construction into the optional-returning template and simplify dispatch/pindex scope. Preserve errors, interruptions, all hashes and the public signature.

Validate coinstats_tests, coinstatsindex_tests and UTXO RPC/hash/index/pruning/AssumeUTXO functional regressions. See [verification report](../../doc/utxostats-backport.md). No deployment or database migration.
