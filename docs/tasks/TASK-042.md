# TASK-042: Reset reconnect backoff after confirmed work

Status: implemented; locked Rust tests pass.

Baseline: be1821d9e7; Rust 1.96.0; SRI pinned unchanged.
Implement and validate this correction in its own commit. Preserve F1-F4,
Core v32.1 IPC and Purity consensus. No dependency patches or production access.
Record red/green evidence and external acceptance gaps.

Per-session confirmation is latched at valid Custom ACK, independently of
subsequent running-state changes. Regression checks consecutive failures through
30 seconds and reset to (wait=1,next=2). Logs: /private/tmp/task042-{red,green}.log.
