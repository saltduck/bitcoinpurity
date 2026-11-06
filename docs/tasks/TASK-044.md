# TASK-044: Generic pool and miner tags

Status: implemented; locked Rust and full Qt tests pass.

Baseline: be1821d9e7; Rust 1.96.0; SRI pinned unchanged.
Implement and validate this correction in its own commit. Preserve F1-F4,
Core v32.1 IPC and Purity consensus. No dependency patches or production access.
Record red/green evidence and external acceptance gaps.

Red: tag construction API was absent. Green: empty and fixed tags, encoded
UTF-8 limits, exact 100-byte boundary, extranonce/witness/output preservation,
Qt fields and config-file persistence pass. Settings JSON remains unused.
Logs: /private/tmp/task044-{red,green,qt-build,qt}.log.
