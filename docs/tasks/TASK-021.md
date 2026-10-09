# TASK-021: Node configuration default updates

Status: implemented and locally verified; no deployment.

Implement the user-specified defaults: wallet fallback 1 sat/vB, block size
3985000 bytes, block weight 3985000 units, mainnet assumevalid block 961631
(`00000000000000000000807f9dc917442a67910426d79ebb2f8aa2149327ce8a`),
relay 0.1 sat/vB, incremental replacement 0.01 sat/vB, mining 0.4 sat/vB
and DATUM difficulty 262144. Synchronize the Qt difficulty and help defaults.
Preserve explicit overrides and the opt-in Core policy profile.

Regression coverage: mainnet chainparams, effective/clamped block-template
limits, native relay and mining fee behavior, wallet startup without a fallback
fee setting, and DATUM startup without a share-difficulty setting.

## Test-first results

The new mainnet assumevalid and effective block-limit unit regressions failed
on the original defaults (one hash mismatch and two capacity mismatches).
The native policy functional regression failed on the old relay floor.
The wallet regression failed with "Fallbackfee is disabled" after removing
the framework's explicit fallback fee. DATUM failed because startup without
`datumdiff` returned 65536 instead of 262144.

Logs: `/tmp/purity-defaults-red-chainparams.log`,
`/tmp/purity-defaults-red-miner.log`,
`/tmp/purity-defaults-red-functional.log`,
`/tmp/purity-defaults-red-wallet.log`, and
`/tmp/purity-defaults-red-datum.log`.

## Final validation

- Release `bitcoind`, `bitcoin-cli`, `test_bitcoin` and the `bitcoinqt` library
  built successfully with wallet/SQLite and DATUM enabled, BDB disabled,
  Qt 5 and `HAVE_DECL_PIPE2=0` on macOS.
- All 147 CTest tests passed, including the new assumevalid and effective
  block-limit cases.
- All seven selected functional tests passed: `feature_policy_defaults.py`,
  `wallet_fallbackfee.py --descriptors`, `feature_datum.py`,
  `p2p_feefilter.py`, `mining_basic.py`, `feature_assumevalid.py` and
  `mempool_accept.py`.
- The native policy regression disables the test framework's Core policy
  profile and coin-age priority selection to isolate the requested relay,
  incremental replacement and mining fee defaults. It verifies a relayable
  0.2 sat/vB transaction stays out of the fee-selected template, a 0.5 sat/vB
  transaction is mined, a small fee increase replaces an existing transaction,
  and explicit relay/incremental settings override the defaults.
- Wallet regression removes the framework's explicit fallback fee, verifies
  the actual fee equals 1 sat/vB, then verifies explicit zero still disables
  fallback. DATUM verifies the omitted difficulty is 262144 before exercising
  existing explicit difficulty and hot-update behavior.
- Debug help reports all eight requested defaults exactly. Fee-rate options
  use coin/kvB units; block weight uses weight units rather than bytes.
- `git diff --check` passed.

The initial compile exposed the old requirement that Core's incremental fee
be below the native default. Fee-filter rounding now orders both defaults
before selecting its bucket floor. The existing P2P fee-filter regression
passed with this change.

The full GUI build stopped in the unchanged macOS `iconutil` resource step
with `Invalid Iconset`. The changed Qt source and the complete `bitcoinqt`
library compiled; the GUI executable/app bundle was not fully validated.
No icon resources were changed.

Commands from the repository root:

```sh
cmake -S . -B build -DENABLE_WALLET=ON -DWITH_SQLITE=ON -DWITH_BDB=OFF -DBUILD_DATUM=ON
cmake -S . -B build -DBUILD_GUI=ON -DBUILD_GUI_TESTS=OFF -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
cmake --build build -j 8 --target bitcoind bitcoin-cli test_bitcoin bitcoinqt
ctest --test-dir build --output-on-failure -j 8
TEST_RUNNER_PORT_MIN=31000 python3 build/test/functional/test_runner.py --jobs=3 --tmpdirprefix=/tmp/purity-defaults-green-suite feature_policy_defaults.py 'wallet_fallbackfee.py --descriptors' feature_datum.py p2p_feefilter.py mining_basic.py feature_assumevalid.py mempool_accept.py
build/bin/bitcoind -datadir=/tmp/purity-maxtipage-help -nosettings -help-debug
git diff --check
```

Logs: `/tmp/purity-defaults-green-targets.log`,
`/tmp/purity-defaults-ctest.log`, `/tmp/purity-defaults-functional.log`,
`/tmp/purity-defaults-green-help.txt` and
`/tmp/purity-defaults-green-build2.log` (GUI resource failure).
