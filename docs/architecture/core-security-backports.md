# Core October 2026 security backports

Adaptations also match the official
[Core 29.5 backport](https://github.com/bitcoin/bitcoin/pull/36666).

- [#36665](https://github.com/bitcoin/bitcoin/pull/36665), commit
  `1e0c407377034e9cbb4d52f1f534c06cdf202a36`: add sockets to select's exception
  set only when RECV is requested. This prevents urgent data from bypassing
  receive backpressure. GenerateWaitSockets already honors fPauseRecv here.
- [#36667](https://github.com/bitcoin/bitcoin/pull/36667), commit
  `cc2534cdf7b14d0f1a18928ebaab7f7fdd7e839e`: record false-to-true fPauseSend
  transitions in both PushMessage and SocketSendData. Use this branch's
  GetTime<seconds>/atomic duration convention instead of upstream NodeClock.
  InactivityCheck disconnects after strictly more than TIMEOUT_INTERVAL (20
  minutes), even when other traffic continues. Subsequent queueing does not
  refresh a pause; draining then refilling starts a new interval.
- [#36669](https://github.com/bitcoin/bitcoin/pull/36669), commits
  `17556cb1f52fcbc807f65245101f72e8ec1573d8` and
  `48092a9042d8fcfd6bb7ae5911a1d53b8746d546`: widen FlatFilePos,
  CBlockFileInfo sizes, CBlockIndex offsets and interfaces::BlockInfo::data_pos
  to uint64_t. In this branch these live in chain.h and interfaces/chain.h,
  rather than upstream's blockstorage.h and kernel/chain.h. Widen allocation
  calculations and truncate/allocate offsets; use FileSeek64/FileTell64 and
  Windows _fseeki64/_ftelli64/_chsize_s. Keep the existing allocation rounding.

The changes implement CVE-2026-117350, CVE-2026-117349 and CVE-2026-117346
mitigations respectively. No dependency source changes are needed.

Regression coverage uses TCP urgent data on select platforms, deterministic
mock-time tests for both send queue paths, VARINT compatibility, and sparse
file operations across 2 GiB and 4 GiB. See TASK-029 for actual validation.
