# Security backport compatibility contract

There are no new RPCs, P2P messages, configuration options or consensus rules.
Configured send/receive buffer thresholds retain their meaning. Peers paused
for sending continuously for more than 20 minutes are disconnected, including
legacy peers lacking nonced ping support. TCP urgent data cannot bypass a
receive pause on select platforms; receive-enabled sockets retain exception
monitoring needed for Windows failed connections.

Block/undo offsets and file byte counters become uint64_t internally. Existing
VARINT-encoded positions retain identical bytes; block/undo payloads, indexes
and RPC output schemas are unchanged. Rebuild consumers of the internal C++
BlockInfo interface because its data_pos member widens. No routine reindex,
rescan or database conversion is required for healthy data.

As in the upstream fix, non-Windows 32-bit file-positioning limits and FAT32's
4 GiB file limit remain. See the
[upstream undo advisory](https://bitcoincore.org/en/2026/10/28/disclose-undo-overflow/).

Rebuild and restart the node to activate these fixes. Existing corruption is
not detected or repaired by this backport. No release, upload or deployment
is part of TASK-029.
