# TASK-PS-000: Baseline and requirement amendments

Stage: S0. Status: baseline and requirement synchronization complete; later acceptance evidence pending.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Evidence:

- Baseline build: Debug, wallet SQLite ON, DATUM/GUI OFF, HAVE_DECL_PIPE2=0.
- Baseline pow_tests, chainparams_tests and transaction_tests passed.
- Preserved baseline binaries and logs under ignored build/baseline/.
- Recorded the owner's no-mode and simulated JSON parameter amendments.

Official history/release/platform gates remain pending.

Documentation-language amendment: all documents for this change, including the
retained v1.0 baseline and source review, are maintained in English. Translation
preserves requirement identifiers, owner amendments and recorded evidence.
