# TASK-045: Reply to unavailable subscriptions before closing

Status: implemented; locked Rust gateway regression passes.

Baseline: be1821d9e7; Rust 1.96.0; SRI pinned unchanged.
Implement and validate this correction in its own commit. Preserve F1-F4,
Core v32.1 IPC and Purity consensus. No dependency patches or production access.
Record red/green evidence and external acceptance gaps.

Red: unavailable peer is reset before a reply. Green: pipelined configure,
authorize and subscribe receive ordered replies, terminal error 20 and EOF.
The writer drains queued replies before closing under existing write timeouts;
no fabricated work or rejected share increment. Noise cancellation still passes.
Logs: /private/tmp/task045-{red,green}.log.
