# TASK-025: Restrict mainnet DNS seeds to the Purity seed

Status: implemented and locally verified; no commit or deployment.

Keep only `seed.bitcoinpurity.org.` in the built-in mainnet DNS seed list in
`src/kernel/chainparams.cpp`. The user confirmed that this change applies only
to mainnet. Other networks' DNS seeds, fixed seeds and explicit peer
configuration are unchanged.

Synchronize the requirement entry, product specification, discovery architecture
and contract, migration report and task index. Add a chain-parameter regression
before changing the seed list, then build and run the chain-parameter tests and
the existing DNS-seeding functional test.

## Validation

The new `chainparams_tests/purity_mainnet_dns_seed` regression failed before
the implementation because the mainnet list had nine entries instead of one.
After removing the other eight entries, the node/CLI/unit-test build passed,
all eight chain-parameter cases passed, and `p2p_dns_seeds.py` passed.
`git diff --check` passed. The full test suite was not rerun.
