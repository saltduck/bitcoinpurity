# TASK-029: Core #36665, #36667 and #36669 security backports

Implement only the three requested security fixes and necessary branch
adaptations described in [requirements](../../input/requirements.md),
[architecture](../architecture/core-security-backports.md) and
[compatibility contract](../api/core-security-backports.md).

## Acceptance

- select sockets without RECV never report urgent-data exceptions; RECV still
  detects them, including when send monitoring is enabled.
- Both send-queue paths begin a pause timer only on a false-to-true transition.
  Continuing traffic and queueing cannot defer its 20-minute timeout. Draining
  cancels the pause; refilling starts a fresh timer.
- File offsets and block/undo size metadata survive values above 4 GiB and
  retain legacy VARINT encoding. Sparse-file positioning, allocation and
  truncation work above 2 GiB and 4 GiB.
- No consensus, external schema, dependency-source or release changes.

## Validation

Validated on 64-bit macOS on 2026-10-31 with AppleClang 21, Release,
DATUM enabled, SQLite wallet enabled and BDB/GUI disabled.

- Test-first: the four new cases reproduced missing paused-send expiry,
  unwanted urgent-data exceptions and truncated large offsets/metadata on
  the original sources. After applying the backports all four pass.
- Built test_bitcoin, bitcoind and bitcoin-cli with CMake/Ninja.
- 103 unit cases passed across flatfile_tests, streams_tests, fs_tests,
  net_tests, sock_tests, blockchain_tests, blockmanager_tests, txindex_tests,
  blockfilter_index_tests, coinstatsindex_tests, netbase_tests,
  net_peer_connection_tests and net_peer_eviction_tests.
- Eight functional runs passed: p2p_timeouts (v1 and v2), p2p_ping,
  p2p_invalid_messages, feature_reindex, feature_reindex_readonly,
  feature_coinstatsindex and p2p_blockfilters.
- git diff --check and new documentation link checks passed.

The initial WERROR build was blocked by existing thread-safety warnings in
net_processing.cpp:3416/3419. The successful build used WERROR=OFF; existing
warnings were left unchanged. Windows file APIs were ported from upstream but
not compiled or runtime-tested on Windows. Linux poll was not runtime-tested.
Non-Windows 32-bit positioning and FAT32 file-size limits remain as documented
by upstream. No commit, push, release or deployment was performed.
