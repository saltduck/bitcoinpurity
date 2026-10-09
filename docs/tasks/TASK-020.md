# TASK-020: Seven-day default maximum tip age

Status: implemented and locally verified; no deployment.

Change `DEFAULT_MAX_TIP_AGE` from 24h to 168h (604800 seconds).
Keep explicit `maxtipage` overrides and other IBD conditions unchanged.
Synchronize requirements, product specification, validation architecture/API
contract and migration report.

Update `feature_maxtipage.py` before the implementation: verify that a tip
older than seven days remains in IBD and a tip exactly seven days old exits
IBD without setting `maxtipage`. Include 24 hours among explicit overrides.

## Validation

- Built the original default with Release, GUI/wallet/DATUM disabled and
  `HAVE_DECL_PIPE2=0` for the local macOS build.
- The updated functional test failed on the original code at the seven-day
  boundary: `initialblockdownload` was true instead of false. Log:
  `/tmp/purity-maxtipage-red.log`.
- Rebuilt after the one-line constant change; `feature_maxtipage.py` passed
  the seven-day default boundary, all explicit overrides (including 86400)
  and the maximum integer value. Log: `/tmp/purity-maxtipage-green.log`.
- Debug help with an isolated datadir and `-nosettings` reports
  `(default: 604800)` for `-maxtipage`.
- `git diff --check` passed.

Commands from the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=OFF -DENABLE_WALLET=OFF -DBUILD_TESTS=OFF -DBUILD_DATUM=OFF -DWITH_MINIUPNPC=OFF -DHAVE_DECL_PIPE2=0
cmake --build build -j 8
TEST_RUNNER_PORT_MIN=31000 python3 test/functional/feature_maxtipage.py --configfile=build/test/config.ini --tmpdir=/tmp/purity-maxtipage-green --portseed=6048
build/bin/bitcoind -datadir=/tmp/purity-maxtipage-help -nosettings -help-debug
git diff --check
```

## Minimum-chain-work regression follow-up

The full functional suite reported that `feature_minchainwork.py` no longer
kept node2 in IBD after all nodes reached height 50. Reproduced the same
`False == True` assertion failure with the current binary before editing.
The test advances node2's clock by two days, which exceeds the original
one-day tolerance but is within the new seven-day default.

Explicitly set `-maxtipage=86400` only on node2, retaining the original time
offset and IBD assertion. No production code changes are needed.
`feature_minchainwork.py`, `feature_maxtipage.py` and
`p2p_headers_sync_with_minchainwork.py` all passed together (3/3).
`git diff --check` passed. The complete 364-test suite was not rerun.

```sh
TEST_RUNNER_PORT_MIN=35000 python3 build/test/functional/test_runner.py --jobs=2 --tmpdirprefix=/tmp/purity-minchainwork-green feature_minchainwork.py feature_maxtipage.py p2p_headers_sync_with_minchainwork.py
```

Failure log:
`/tmp/purity-minchainwork-red/test_runner_₿_🏃_20261009_133250/feature_minchainwork_0/test_framework.log`.
Passing log: `/tmp/purity-minchainwork-green.log`.
