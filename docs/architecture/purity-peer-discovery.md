# Purity peer discovery

Local services flow from init to CConnman, PeerManager VERSION and local
CAddress advertisements. VERSION updates AddrMan with SetServices; addr/addrv2
gossip adds advertised flags with Add. Existing uint64/CompactSize encodings
and peers.dat retain bit 25 without a format change.

ThreadOpenConnections requests a service preference for the first 20 attempts
of automatic non-anchor, non-feeler selection. AddrMan scans its existing
records for matching service hints within the selected networks/table. It
randomly samples matches using the existing GetChance penalty, new-record
multiplicity, and the 50% new/tried split. When no hints match, it immediately
uses its original bucket selector. No persistent index/database is added.
The scan is linear in AddrMan size and only occurs during these connection
attempts; normal selection/feelers retain the original path.

All outbound eligibility checks still apply after selection. After 20 attempts
selection uses the original path, so connected, unreachable or recently tried
preferred addresses cannot prevent ordinary fallback. This limit precedes the
existing 30-attempt recent-try and 50-attempt bad-port relaxations. Manual
connections, anchors, feelers, DNS bootstrap and the 100-attempt cap are retained.

ProcessNewBlockHeaders still applies the consensus-pinned activation hash.
PeerManager retains the existing mismatch demotion/disconnection behavior.
Service advertisement cannot grant chain identity or skip header validation.

ContextualCheckBlockHeader logs an activation-block hash mismatch with
LogDebug in BCLog::VALIDATION, including the height and received/pinned hashes.
The mismatch is expected when receiving headers from a non-Purity chain;
it remains rejected as bad-purity-activation-block. LoadBlockIndex retains
its ERROR and returns false for a conflicting activation block already stored
locally, preventing startup with that index.
