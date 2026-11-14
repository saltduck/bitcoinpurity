# TASK-043: Encode negotiated extranonce as data

Status: implemented; locked Rust tests pass.

Baseline: be1821d9e7; Rust 1.96.0; SRI pinned unchanged.
Implement and validate this correction in its own commit. Preserve F1-F4,
Core v32.1 IPC and Purity consensus. No dependency patches or production access.
Record red/green evidence and external acceptance gaps.

Red: final-push and negotiated-size assertions fail against the raw extranonce
layout. Green: 12/16/32 sizes, declared/custom prefix equality, witness/payout
preservation and CHECKMULTISIG bytes as data pass. Original 16-byte fixture
remains unchanged. Logs: /private/tmp/task043-{red,green}.log.
