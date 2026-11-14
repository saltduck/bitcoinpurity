# TASK-047: Configurable periodic SV2 job refresh

Status: implemented; local Linux/macOS verification passes. Native Windows and
physical ASIC compatibility remain separate acceptance gates.

Default 30 seconds, inclusive range 30–60; CLI/config/Qt, restart required.
Coalesce ordinary transaction changes, refresh even without transactions, and
never defer a new chain tip/reorg. Preserve acknowledged-job publication, old
same-parent work and per-job share accounting. No dependency code changes.

Regression tests are added before production changes. A mocked monotonic clock
covers 30/45/60 seconds, continuous mempool updates without deadline starvation,
idle refresh and immediate new tips. Qt covers default/range and configuration
persistence. Functional tests cover startup rejection and real external software
miner shares, block acceptance, propagation and same-parent delayed work.

The implementation changes only local template scheduling and configuration;
SV2 protocol, IPC schemas and dependency sources are unchanged.

## Evidence (2026-11-03, Australia/Melbourne)

Base: `987d42a220`; results apply to the accompanying TASK-047 commit.
All logs and build directories are outside the checkout under `/private/tmp`.

- Red: new `miner_tests/sv2_job_refresh_interval` failed on the unchanged
  implementation (transaction changes bypassed the interval and steady time
  could not be advanced deterministically). Qt failed because the interval
  control did not exist. Logs: `task047-red.log`, `task047-qt-red.log`.
- Green: enabled macOS ARM64 and Linux ARM64 builds and 157/157 CTests each,
  including Qt and Core v32.1 IPC schema checks. After adding configured-value
  loading coverage, the final Qt suites pass again on both platforms.
- Unit coverage uses a mocked monotonic clock at 30/45/60 seconds; a real valid
  mempool transaction is absent from cached work and included at the deadline.
  Repeated transaction-update signals neither refresh early nor postpone work.
  Idle deadlines create new templates and a new parent bypasses the interval.
- macOS ASan/UBSan: all 10 miner test cases pass; unsupported leak detection is
  disabled. No sanitizer finding. Log: `task047-asan.log`.
- macOS full `feature_sv2.py` passes with CLI 30 overriding configuration 60;
  startup rejects 29/61. External software PoW, pool ACK, local block acceptance,
  second-node propagation, delayed same-parent shares/solutions, reorg and
  reconnect/rejection assertions are preserved. Log: `task047-functional-final.log`.
- One existing functional rejection wait assumed a transaction immediately
  triggers declaration within 20 seconds. Its first run failed under the new
  contract. The wait is now interval + 20 seconds; the rejection/recovery checks
  remain intact. The actual refresh timing has additional assertions.

- Linux full `feature_sv2.py --job-interval=60` passes using the configuration
  file alone for the 60-second interval. It repeats real share/block PoW, second
  node propagation, same-parent old work, reorg, rejection/recovery and failure
  checks. Log: `purity-sv2-linux/task047-functional-60.log`.
- SV2-OFF macOS daemon/CLI/Qt build passes without a Rust toolchain invocation.
  Log: `task047-off-build.log`. `git diff --check` passes.

Native Windows and physical ASIC firmware compatibility are not verified by
these runs. No production endpoints or external pool source are used.
