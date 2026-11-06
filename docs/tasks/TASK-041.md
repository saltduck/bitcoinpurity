# TASK-041: Stage-aware diagnostics

Status: implemented; locked Rust regression passes.

Baseline: be1821d9e7; Rust 1.96.0; SRI pinned unchanged.
Implement and validate this correction in its own commit. Preserve F1-F4,
Core v32.1 IPC and Purity consensus. No dependency patches or production access.
Record red/green evidence and external acceptance gaps.

Red: encrypted SetupConnection.Error was reduced to generic protocol failure.
Green: encrypted response/error-code assertion, existing F1-F4 and wire tests pass.
Logs: /private/tmp/task041-{red,green}.log. RPC/Qt build acceptance follows in TASK-046.
