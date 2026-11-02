# TASK-040: Isolate Cargo artifacts

Status: implemented; local macOS build verified.

Baseline: be1821d9e7; Rust 1.96.0; SRI pinned unchanged.
Implement and validate this correction in its own commit. Preserve F1-F4,
Core v32.1 IPC and Purity consensus. No dependency patches or production access.
Record red/green evidence and external acceptance gaps.

Red: fixture and library originally shared target directory. Green: parallel
bitcoind/sv2_test_pool build and incremental rebuild pass with separate outputs;
production archive hash and mtime remain unchanged when rebuilding the fixture.
Logs: /private/tmp/task040-{build,incremental}.log. MSVC execution and final
disabled-build regression remain tracked under TASK-046.
