# TASK-PS-006: Real parameters and first-block pin freeze

Stage: S6. Status: simulated freeze rehearsal passed; official parameters deferred.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

The configuration holds H, H-1/anchor/parent hashes and anchor bits/time,
ordered public keys, first-block pin, minimum chainwork, ports and seeds.
The owner explicitly requested initial simulation values. H=11 uses the real
first ten Bitcoin Signet blocks; the provisional first-block pin is null.

Reference rehearsal passed against both the separate repository baseline and
official Bitcoin Core 28.0 (archive checksum verified by the repository's
previous-release tool). Every hash 0..10 and the UTXO hash/amount/output count
matched. A new signed PoW H block was archived, its hash written into a pinned
simulation JSON, and startup with that pin passed. The Core reference accepted
the actual original block 11 on its own continuation. Tests isolate ports,
cookies, wallets and chain directories. H selection is now deferred to launch. Official custody/seeds and public release records
remain pending.

Latest owner amendment: select H=T+1 from the fully validated, paused tip at
launch; H=318621 is obsolete rehearsal data. TASK-PS-008 adds schema v2
preparation, snapshot generation and safe index conversion. Keep H=11 as a
regression fixture. Official custody, operated seeds, first-block pin and full
launch-history/UTXO evidence remain pending.
