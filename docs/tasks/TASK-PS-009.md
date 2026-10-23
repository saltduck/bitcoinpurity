# TASK-PS-009: Miner runtime status logging

Status: implemented and locally verified. No deployment, commit or PR.

Requirements: timestamped stderr logs for remaining wait time, signer requests,
quorum/proposal validation, elapsed PoW heartbeats, submission, active-chain
acceptance, stale work and retries. Preserve stdout JSON, configuration schema,
signing/PoW checks, the 600-second scheduling gate and consensus. Never log RPC
credentials, PSBTs, signature bytes or raw block/header payloads. Imported modules
must not configure the caller's root logger.

## Implementation

Use the existing poll interval to report waiting and to poll one PoW process.
Communicate timeout expiry emits an elapsed-time heartbeat and leaves that
process running; no total solver timeout or fake percentage/hashrate is added.
An interrupted call kills and reaps its child, matching the former subprocess
run cleanup. Direct CLI execution configures stderr logging, while accepted
block JSON remains the only normal stdout output.

## Verification

Added tests before implementation. The original code failed waiting/status,
heartbeat and CLI timestamp expectations. Fourteen isolated tests now pass,
covering countdowns without signing, all successful stages, private payload
omission, repeated heartbeat timeouts without restarting the solver, interruption
cleanup, failed quorum, stale work, invalid signed fields/PoW, rejected or inactive
submissions, unchanged stdout JSON, safe CLI errors and import logging behavior.
An actual delayed Python solver verifies that communicate timeouts emit a
heartbeat and then return valid work from the same child without leaking its
stderr. Continuous-mode errors also retain the configured retry delay.
Both registered Signet utility CTests passed; the miner CTest was rerun after
adding the real-process and retry cases. The functional regression also
checks actual two-wallet signing/status output and the stdout JSON contract.

Both four-node functional variants passed: the real-cadence run took 604 seconds
and the fast run took 21 seconds. They cover genuine wallet signatures and PoW,
continuous retry during a signer outage, recovery, an alternate signer pair,
restoration from wallet backup, exact GBT bits and synchronized validated tips.
The real-cadence run retained the actual 600-second waiting rule. These runs use
isolated H=11 fixtures, not the operator's official chain or signing wallets.
Syntax/documentation link checks and `git diff --check` passed.

Evidence: `build/signet-miner-logging-before.log`,
`build/signet-miner-logging-configure.log` and
`build/signet-miner-logging-functional.log`. Existing production processes and
operator configurations are not restarted or modified by this task.
