# TASK-037: SV2 pre-release correctness and independent interoperability

Status: in progress. PR #44 must remain draft; no merge or production deployment.
Review baseline: e7cd08c6c85e2c62a97096d7e54e84df5e9c2a61.
SRI remains pinned to d1d30f3e755b0a5108633e1be7012034a8caaecd.
Independent CKPool reference: e04e45fbb55a76250f4504f8d8865e1ad0fc0cf2.

- F1: protocol-specific SetupConnection success semantics. Add flags=0 positive
  tests, extended-channel requirement and fixed-version conflict cases; fixture
  must return legal flags, not echo requests. Explicit unsupported flags remain
  SetupConnection.Error.
- F2: same-parent templates retain old jobs, targets and timestamps; clean_jobs
  only on consensus header context change. Delayed old shares and blocks must
  remain valid; real tip/reorg invalidates them.
- F3: local, Mining and JDS solution submission outcomes must be independent.
  Every attempted share must reach accepted, rejected or abandoned accounting.
- F4: count parsed local rejections once, including unknown work, saturation and
  pending limits. Unacknowledged sent shares are abandoned, never accepted.

Acceptance still required: targeted regressions, Linux/macOS reruns, unmodified
CKPool Mining/JDS with a Purity IPC validator and external V1 PoW, plus native
Windows MSVC/Qt and release packages. No production endpoints or miners may be used.

## Evidence

- F1: legal-zero success first failed in a unit regression. After correction,
  locked Rust tests and the full local V1/SV2/JDS functional test pass. The
  fixture now returns zero success flags; unsupported features produce a real
  SetupConnection.Error. Mining fixed-version conflicts are rejected.
- F2: same-parent refresh retains old SRI jobs and per-job timestamps/targets.
  A mutation restoring unconditional tip updates fails with stale-share. A V1
  functional regression refreshes the mempool before submitting delayed J1
  share and network block solutions; both are accepted. Parent change/reorg and
  clean_jobs assertions also pass. This is fixture evidence, not CKPool evidence.
- F3: local submission runs before upstream writes; pending is recorded before
  Mining is attempted, followed by JDS even if Mining fails. Both write results
  are retained and sanitized errors identify the failed path. Injected future
  failures/timeouts prove the orchestration attempts both paths. Actual
  independent socket-write fault injection and ACK/accounting checks remain pending.
- F4: missing runtime job, pending cap, V1 unknown work, ingress queue failure
  and reconnect queue drain now count local rejection. Existing duplicate,
  unknown-work and lost-ACK functional assertions check exact rejected/abandoned
  changes. Deterministic queue and pending saturation, upstream rejection and
  mixed batch/error accounting acceptance remain pending.
- Current macOS and Linux locked Rust tests, 156 CTests per platform, complete
  Qt suites and full V1/SV2/JDS functional reruns pass. Linux initially used an
  outdated test-pool binary because that target is excluded from the default
  build; the added fixed-version test failed. Explicitly rebuilding sv2_test_pool
  and rerunning the unchanged assertions passed. These are current fixture
  results, not independent protocol compliance or native Windows acceptance.

## Independent CKPool blocker

The unmodified reference source was checked out outside this project. No CKPool
or SRI source was patched. CKPool ipc/mining/mining.capnp and Purity
src/ipc/capnp/mining.capnp share schema ID 0xc77d03df6a41b505, but CKPool method @5
is checkBlock while Purity @5 is createNewBlock2. Method @4 also has different
parameter layouts, and BlockTemplate ordinals diverge. A direct connection is
not a valid interoperability test and cannot supply CKPool's required consensus
validation backend. No CKPool binary E2E or production compatibility is claimed.

Decision requested: authorize a separate Core-compatible IPC socket/adapter
owned by this project, preserving existing Purity IPC and unmodified dependencies,
or defer independent CKPool acceptance while finishing bounded F1–F4 regressions.
Do not silently replace the existing schema or repurpose occupied ordinals.

Decision (2026-11-02): migrate the existing IPC endpoint to Core v32.1 under
TASK-038. The separate adapter/socket proposal above is superseded. CKPool must
update its own v31-era bindings; no compatibility with its unique snapshot is
required. Independent pool and outstanding F3/F4 acceptance gates remain open.
