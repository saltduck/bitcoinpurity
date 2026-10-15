# TASK-PS-004: Shared-magic P2P history and fork isolation

Stage: S4. Status: core implementation and local shared-magic regression passed.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Headers presync/redownload enforce Signet history pins before the work
threshold. Signet presync stops at sufficient work; full validation classifies
the remaining fork suffix and credits the accepted prefix. Historical seed
endpoints bootstrap address discovery while the prefix is needed and
automatic connection configuration permits it.

Service claims alone do not earn relevant-service/protected status. Peers must
provide a fully validated H-or-later block; a known first-block header triggers
a full-block proof request. Unverified automatic outbound peers expire 20
minutes after the local node reaches H-1. Existing bounded stale demotion keeps
useful history peers while the prefix is missing. Expected pin conflicts do
not discourage or ban peers.

Red tests reproduced incorrect prefix protection and missing presync pins.
Green units cover original non-Signet protection, new Signet behavior,
both presync stages and unverified-peer timeout. The functional test synchronizes
the ten real prefix blocks from a peer forging both Purity service flags,
retains history before an H conflict, disconnects it without banning, then
qualifies a valid full-block peer. A separate unmodified baseline node also
served the complete simulated prefix over the shared magic.
