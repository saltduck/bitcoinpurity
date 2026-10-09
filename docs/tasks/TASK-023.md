# TASK-023: Activation-block mismatch DEBUG logging

Status: implemented and locally verified; no deployment.

Resolve issue #32 by logging the received activation-block hash mismatch with
`LogDebug(BCLog::VALIDATION, ...)`, retaining height and both hashes and removing
the `ERROR:` message prefix. Keep `bad-purity-activation-block`, consensus and
peer handling unchanged. Local block-index conflicts must still log ERROR and
prevent startup. No database migration is needed.

Synchronize the requirement entry, product specification, peer-discovery
architecture/API contract, migration report and task index.

## Validation

- Added both regressions before changing production code. On the original
  implementation, the header test failed because the category-disabled run
  emitted the message and both runs produced two messages instead of one.
  The stored-index ERROR/load-failure regression already passed.
- After the logging change, both regressions passed: valid-PoW conflicting
  header rejection, category-disabled silence, category-enabled DEBUG content,
  permitted pinned-header acceptance, and stored-index ERROR/load failure.
  The shared fixture restores the previous logger configuration on teardown.
- All 12 related validation, chainparams, peer and logging CTest suites passed.
- All 149 CTest tests passed. `p2p_purity_services.py` passed under both V1
  and V2 transports (2/2 variants). The full functional suite was not run.
- `git diff --check` passed; all 23 task-index links are unique and resolve,
  and the issue #32 requirement matches the product specification.

Commands from the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=OFF -DBUILD_TESTS=ON -DBUILD_DATUM=OFF -DWITH_MINIUPNPC=OFF -DHAVE_DECL_PIPE2=0
cmake --build build -j 8 --target test_bitcoin
build/bin/test_bitcoin '--run_test=validation_chainstatemanager_tests/purity_activation*' --log_level=test_suite
cmake --build build -j 8
ctest --test-dir build --output-on-failure -j 8 -R 'validation.*tests|chainparams_tests|denialofservice_tests|peerman_tests|logging_tests'
ctest --test-dir build --output-on-failure -j 8
TEST_RUNNER_PORT_MIN=31000 python3 build/test/functional/test_runner.py --jobs=1 --tmpdirprefix=/tmp/purity-issue32-functional p2p_purity_services.py
git diff --check
```

Logs: `/tmp/purity-issue32-red.log`, `/tmp/purity-issue32-green.log`,
`/tmp/purity-issue32-focused.log`, `/tmp/purity-issue32-ctest.log` and
`/tmp/purity-issue32-functional.log`. The pre-push hook requires a clean
worktree and repeats the build/all-CTest checks before the push.

Local build: Release, descriptor wallet enabled, GUI/DATUM/UPnP disabled,
`HAVE_DECL_PIPE2=0` for macOS. No deployment.
