# TASK-PS-011: Automatic miner signer failover

Status: implemented and locally verified. No commit, push, PR, deployment or
operator configuration change.

Scope: the independent Python Signet miner, its simulation example, tests and
English documentation. Keep two-endpoint files compatible and accept three
distinct endpoints for ordered 1+2, 1+3, 2+3 pair failover. Bound signer CLI calls
with a positive integer signer_timeout_seconds (default 30). Start each pair
from the original PSBT, exclude RPC failures/invalid responses per attempt,
and require actual complete node finalization. Preserve immutable fields,
proposal validation, real PoW, stale-tip/submission checks and private logs.
No C++ consensus, node RPC, wallet, dependency or chain-parameter changes.

Test-first evidence: before changing miner.py, the expanded unit suite failed
with six failures and six errors (21 tests). The old implementation rejected
three endpoints and lacked signer timeout support. Those tests passed after
implementation; an additional signed-proposal rejection regression brings the
final suite to 22 passing tests. Logs are local ignored
build/signet-failover-before.log, build/signet-failover-after.log and
build/signet-failover-ctest-final.log.

Functional regressions use four isolated test daemons and the H=11 fixture:
stop A/B/C in turn without editing the three-endpoint config; reject a wallet
without an authority key and duplicate-key endpoint quorum; bypass a locked
wallet; pause below quorum and resume in the same continuous miner process when
one daemon returns. Keep the existing two-endpoint outage and restored-backup
signing scenarios.

The expanded four-daemon functional run passed in 128 seconds and again in 99
seconds in the combined runner, with genuine
wallet signatures, exact GBT bits, real PoW, accepted blocks, synchronized tips
and final verifychain(4,0). The existing real-cadence variant passed in 612
seconds, retaining the actual 600-second wait. Both registered Signet utility
CTests passed (22 miner tests and 10 preparation tests). Python/JSON syntax,
local Markdown links and git diff --check passed. Logs are local ignored
build/signet-failover-fast-final.log and build/signet-failover-functional.log.
No production signer, wallet or mining process was exercised. No C++ node or
consensus source changed; no rebuild, reindex or public-network run was needed.
