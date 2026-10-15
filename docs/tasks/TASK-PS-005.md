# TASK-PS-005: Independent signing miner and RPCs

Stage: S5. Status: independent miner and local wallet/PoW regression passed.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Implemented contrib/purity-signet/miner.py and public sample configs. Two
distinct wallet CLI argument arrays sign the virtual PSBT. The miner preserves
the block/virtual transactions through signing, finalizes after both responses,
verifies the signed proposal, grinds exact GBT bits and submits real PoW.
Stale work is discarded. RPC payloads use stdin; errors do not print credentials
or signing RPC arguments.

GBT returns the template-height challenge. Blockchain/mining info expose
the next challenge height; blockchain info discloses fork, anchor,
old/new challenge, fixed magic/limit and timing. Peer info exposes Signet
full-block qualification.

The functional test runs four separate daemon/data-directory instances. Three
signing wallets each hold one test-fixture key. It proves A+B signing, stops B
while continuous mining stays alive without submitting unsigned work, restores
B and resumes, then restores A's backup on another daemon and signs with A+C.
Exact GBT bits, real PoW, synchronized tips and verifychain(4,0) pass. The fast
run advances mocktime; a separate --real-cadence run passed without mocktime,
with 602 seconds between block timestamps and about 610 seconds between actual
acceptance events. This is one interval, not sustained production evidence.
No production keys were created. Independent-host custody and long-running
operations remain release gates. See ../purity-signet-operations.md.
