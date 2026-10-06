# TASK-017: Activate mainnet RDTS grandfather correction at 961637

Status: implemented and locally verified (full build, 64 focused C++ cases,
149/149 CTest tests and 6/6 functional tests passed).

The user explicitly selected mainnet `nReducedDataGrandfatherFixHeight = 961637`.
Set it to `nPurityActivationHeight`, preserving the existing creation boundary
and all other network parameters. No deployment or data-directory rebuild.

TDD: update the mainnet parameter test first; it fails on the old value
`2147483647`. Test the actual mainnet helper at activation, the next block, the
BIP9 deadline, the reported scan tip and a later height. Retain existing
before/at/after correction and BIP9 transition regressions.

Evidence limits: the user reports no obvious dependent confirmed transactions
in an active-chain candidate scan from 961637 through 967297. Tip hash, chainwork
and a complete corrected-rule historical revalidation were not supplied. This
change records the selected consensus parameter, not proof of historical
compatibility or a network rollout.

## Verification

```sh
cmake --build build --target test_bitcoin -j 8
build/bin/test_bitcoin --run_test=chainparams_tests/reduced_data_mainnet_correction_at_purity_activation --log_level=message
cmake --build build -j 8
build/bin/test_bitcoin --run_test=chainparams_tests,versionbits_tests,pow_tests,validation_chainstate_tests,validation_chainstatemanager_tests --log_level=message
ctest --test-dir build --output-on-failure -j 8
python3 build/test/functional/test_runner.py --keepcache --cachedir=/tmp/purity-rdts-mainnet-cache --jobs=3 feature_purity_rdts_grandfather.py feature_rdts.py feature_bip9_max_activation_height.py feature_reduced_data_utxo_height.py feature_reduced_data_temporary_deployment.py feature_park_deep_reorg.py --tmpdirprefix=/tmp/purity-rdts-mainnet-functional
git diff --check
```

The first single-case run failed as expected before the parameter change
(exit 201, `2147483647 != 961637`). After the change all verification commands
passed. Logs: `/tmp/purity-rdts-mainnet-red.log`,
`/tmp/purity-rdts-mainnet-build.log`, `/tmp/purity-rdts-mainnet-unit.log`,
`/tmp/purity-rdts-mainnet-ctest.log`,
`/tmp/purity-rdts-mainnet-functional.log`.

Production source diff: one mainnet parameter assignment in
`src/kernel/chainparams.cpp`. Existing chainstate data is not automatically
revalidated by this parameter assignment; the documented isolated archival
revalidation remains separate from local test verification.
