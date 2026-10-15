# TASK-PS-001: Signet parameter configuration and isolation

Stage: S1. Status: parameter reader and local startup/identity regression passed.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Changes:

- -signetparams JSON, simulated sample and embedded test fixture.
- Existing Signet parameter constructor; no extra chain type or mode.
- Legacy challenge/blocktime options rejected; public-key and anchor validation.
- signet/ storage and JSON P2P/RPC ports; Mainnet ignores the file.
- Missing configuration rejects actual Signet startup; help does not load it.
- Signet AssumeUTXO table cleared; default assumevalid=0.

Red configuration cases failed before the reader. Initial 53 combined
pow/chainparams/transaction/validation cases passed after the reader. Negative-key/target and actual startup checks also passed.

Verified local evidence:

- Final 53 combined unit cases passed, including malformed/duplicate config,
  invalid/duplicate keys, bad compact targets and port conflicts.
- Offline daemon + CLI: chain=signet, inherited genesis, magic 0a03cf40,
  configured RPC port 38332 and signet/chainstate.
- Missing Signet config rejected startup with exit 1.
- Mainnet daemon started with a deliberately missing Signet config; it was
  never loaded. Both smoke-test nodes were stopped after inspection.

The public bitcoin.conf/miner JSON examples were smoke-tested with the real
daemon/CLI in an isolated directory. Both use a single chain selector; CLI
arrays do not combine -signet with the example's chain=signet setting.
S2/S4/S5 now have their own completed local evidence. Official values remain
an external release decision.

2026-10-15 amendment: defaults 38333/38332 and signet/. The launch file uses
H=318621; H=11 remains a fast fixture. Complete-field validation now rejects
an omitted activation_hash, while explicit null remains allowed in simulation.
