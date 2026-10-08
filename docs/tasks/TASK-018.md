# TASK-018: Standalone Core #34358 wallet removal fix

Status: implemented and locally verified; no deployment.

Backport only the equal_range/iterator-erase fix from merged Core PR #34358.
Keep Purity's uint256 wallet interfaces and successful database-commit
callback. Do not import #34283 or upstream m_txos bookkeeping.

Extend wallet_importprunedfunds.py with one- and two-input replacements,
failed deletion, transaction confirmation, exact outpoint availability,
balance checks, valid coin selection, reload and sole-spend removal. Add a
wallet unit regression for three conflicts, sequential deletion, cleanup only
after commit and abort after partial deletion failure.

The functional regression fails on the original code at the assertion that
the original outpoint remains spent after deleting the replaced transaction.
The unit regression also fails on the original code (exit 201, nine failed
checks): both inputs lose their surviving spends after the first deletion.

Existing wallet formats remain unchanged. Normal upgrades require a rebuild
and process restart, with no wallet migration, rescan or chain reindex.
Previously inconsistent state is not automatically repaired.

## Files and final diff review

- src/wallet/wallet.cpp: replace erase(outpoint) with the upstream matching
  iterator loop; all surrounding commit, ordering, mapWallet, notification and
  MarkDirty code is unchanged. No wallet.h or serialization changes.
- test/functional/wallet_importprunedfunds.py: adapt inputs to send's options,
  use explicit opt-in RBF, isolated wallet funding and outpoint pairs, and add
  the requested confirmation/reload/balance/failure coverage.
- src/wallet/test/wallet_tests.cpp: add RemoveTxs_conflicting using the existing
  SQLite test wallet and transaction APIs.
- input/requirements.md, docs/requirements/product-spec.md,
  docs/architecture/wallet-transaction-removal.md,
  docs/api/wallet-transaction-removal.md, docs/migration-report.md,
  docs/INDEX.md, docs/tasks/README.md and this task: required documentation sync.

The production loop matches merged upstream commit
cd1af852fa5d919d8a6dc0a67cb10f0a5652fb77. Purity retains uint256 instead of
upstream Txid and does not introduce m_txos. The regression uses a native
inequality assertion because assert_not_equal is unavailable. No #34283
changes were imported. Concurrent edits to feature_datum.py and
feature_park_deep_reorg.py are outside this task and were preserved.

## Executed validation

- cmake --build build -j 8: passed with existing Debug/sanitizer configuration,
  ENABLE_WALLET=ON, WITH_SQLITE=ON, WITH_BDB=OFF, BUILD_GUI=OFF.
- test_bitcoin '--run_test=wallet_tests/RemoveTxs*' --log_level=message:
  both original and new removal cases passed.
- CTest wallet/RBF selection: 12/12 suites passed (coinselector_tests,
  wallet_tests, wallet_crypto_tests, walletload_tests, spend_tests, rbf_tests,
  psbt_wallet_tests, scriptpubkeyman_tests, walletdb_tests,
  wallet_transaction_tests, ismine_tests and wallet_rpc_tests).
- Functional descriptor tests: wallet_importprunedfunds.py,
  wallet_conflicts.py, wallet_abandonconflict.py, wallet_bumpfee.py and
  wallet_multiwallet.py all passed (5/5).
- wallet_migration.py: skipped, previous releases not available or disabled.
- wallet_importprunedfunds.py --legacy-wallet: skipped, BDB not compiled.
- Separate build-no-wallet configuration and full build passed with
  ENABLE_WALLET=OFF, BUILD_WALLET_TOOL=OFF, BUILD_GUI=OFF, BUILD_TESTS=OFF,
  BUILD_FUZZ_BINARY=OFF; retain Debug, both sanitizers and BUILD_DATUM=ON.
  Its daemon -version ran successfully with an isolated datadir and -nosettings.
- git diff --check: passed.

Commands (from the repository root):

```sh
cmake --build build -j 8
export UBSAN_OPTIONS="suppressions=$PWD/test/sanitizer_suppressions/ubsan:print_stacktrace=1:halt_on_error=1:report_error_type=1"
build/bin/test_bitcoin '--run_test=wallet_tests/RemoveTxs*' --log_level=message
ctest --test-dir build --output-on-failure -j 3 -R '^(wallet.*tests|psbt_wallet_tests|spend_tests|coinselector_tests|scriptpubkeyman_tests|ismine_tests|rbf_tests)$'
export UBSAN_OPTIONS='silence_unsigned_overflow=1:halt_on_error=1:print_stacktrace=0'
python3 build/test/functional/test_runner.py --jobs=3 --keepcache --cachedir=/tmp/purity-34358-cache2 --tmpdirprefix=/tmp/purity-34358-functional2 'wallet_importprunedfunds.py --descriptors' 'wallet_conflicts.py --descriptors' 'wallet_abandonconflict.py --descriptors' 'wallet_bumpfee.py --descriptors' 'wallet_multiwallet.py --descriptors' wallet_migration.py
python3 test/functional/wallet_importprunedfunds.py --configfile=build/test/config.ini --legacy-wallet --tmpdir=/tmp/purity-34358-legacy
cmake -S . -B build-no-wallet -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_WALLET=OFF -DBUILD_GUI=OFF -DBUILD_FUZZ_BINARY=OFF -DBUILD_TESTS=OFF -DBUILD_WALLET_TOOL=OFF -DWITH_MINIUPNPC=OFF -DHAVE_DECL_PIPE2=0 -DSANITIZERS=undefined,unsigned-integer-overflow -DCMAKE_CXX_FLAGS="-fsanitize-ignorelist=$PWD/test/sanitizer_suppressions/ubsan"
cmake --build build-no-wallet -j 8
git diff --check
```

The first functional batch was blocked during cache creation by macOS atos
symbolizer warnings on stderr, before any selected test ran. The successful
retry silenced unsigned-overflow diagnostics (expected in cryptographic
arithmetic), disabled stack printing and kept undefined-behavior errors fatal;
no source or production configuration was changed for this workaround.

Logs are in /tmp/purity-34358-red2.log, /tmp/purity-34358-red-unit.log,
/tmp/purity-34358-build.log, /tmp/purity-34358-unit-focused.log,
/tmp/purity-34358-unit.log, /tmp/purity-34358-functional2.log,
/tmp/purity-34358-legacy.log and /tmp/purity-34358-no-wallet-build.log.

## Limits

Wallet formats remain compatible by inspection; descriptor loading and normal
transaction history were exercised. Legacy/BDB runtime behavior and migration
were not executed. No production wallets or chainstate were modified.

An exploratory abort after successful removal registration hit the existing
empty on_abort callback (std::bad_function_call). The final test checks abort
after partial failure before listener registration; commit/erase failure
injection and clean abort after registration remain unverified. This patch
preserves the existing transaction-handling code and does not fix that separate
issue. See the migration report for runtime-state repair limits.
