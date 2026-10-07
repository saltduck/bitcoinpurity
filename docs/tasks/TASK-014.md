# TASK-014: Read-only RDTS grandfather audit

## Requirement

Add `auditrdtsgrandfather` exactly on v1.0.0 without fixing dynamic grandfathering.
See input/requirements.md and docs/api/rdts-grandfather-audit.md.

## Implementation and acceptance

- Snapshot active ancestry/tip; read only blk/rev; use undo Coins and all spent
  outputs for native old/strict per-vin checks.
- Return four totals, bounded reproduction details, completeness and tip change;
  log progress/summary and respect shutdown. Isolate validation caches.
- Cover candidate boundaries, flags, mixed inputs, classifications, missing data,
  every RDTS spend-side flag family and real mixed-input Taproot sighashes.
- Wallet-free functional test compares tip, height, UTXO hash, chain tips, header,
  and block/undo file hashes; covers CLI parameters and pruned history.
- Run new tests plus feature_rdts.py, feature_rdts_ignore_rejects.py and
  rpc_blockchain.py; preserve production consensus bodies.
- No real mainnet audit is included in local acceptance.

## Local verification (2026-10-06)

Completed on macOS arm64, using an isolated build with wallet/GUI/DATUM disabled.
The macOS SDK pipe2 declaration is disabled for compatibility with the host runtime.

```sh
cmake -S . -B build -G Ninja -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DBUILD_DATUM=OFF -DENABLE_WALLET=OFF -DWITH_ZMQ=OFF -DWITH_USDT=OFF -DHAVE_DECL_PIPE2=0 -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target bitcoind bitcoin-cli test_bitcoin -j 6
build/bin/test_bitcoin --run_test=rdtsgrandfather_tests --log_level=test_suite
python3 build/test/functional/test_runner.py --cachedir=/private/tmp/rdts-audit-cache --tmpdirprefix=/private/tmp/rdts-audit-functional --jobs=4 rpc_auditrdtsgrandfather.py feature_rdts.py feature_rdts_ignore_rejects.py rpc_blockchain.py
python3 build/test/functional/test_runner.py --cachedir=/private/tmp/rdts-audit-cache --tmpdirprefix=/private/tmp/rdts-audit-functional --jobs=1 rpc_auditrdtsgrandfather.py
git diff --check
```

- TDD red: new C++ test object initially failed because the helper header did not
  exist; the tests were added before implementing the RPC/helper.
- Final build: PASS. An initial link failure from an unavailable optional<int>
  RPC template was corrected to use v1.0.0's supported parameter parsing.
- New C++ suite: PASS, all four cases. Covers all OP_SUCCESSx and signed mixed
  Taproot inputs, in addition to the required boundaries and RDTS families.
- Existing functional tests: PASS, feature_rdts.py, feature_rdts_ignore_rejects.py,
  and rpc_blockchain.py with both v1transport and v2transport.
- New functional test: initial fixture failure (modified coinbase retained its
  cached txid) corrected with rehash(); final isolated retry PASS (7 seconds).
  Read-only state/file hashes, same-block spends, CLI/ranges, genesis without
  undo, and missing pruned block/undo records all passed. The first combined
  invocation returned failure because of that new-test fixture issue.
- Whitespace check: PASS. Scripted baseline comparison confirmed byte-identical
  ConnectBlock and GetBlockScriptFlags bodies, deploymentstatus, versionbits and
  blockstorage source/headers (only GetBlockScriptFlags linkage is exposed).
- No production deployment or real mainnet audit was performed. No mainnet
  compatibility conclusion follows from these local tests.
