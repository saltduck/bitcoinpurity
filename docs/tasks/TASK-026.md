# TASK-026: Replace mainnet fixed seeds with selected Purity nodes

Status: implemented and locally verified; no commit or deployment.

Replace the complete mainnet fixed-seed array and its `nodes_main.txt` source
with the user's 11 endpoints. Preserve ports 5890 and 8334 and both IPv6
addresses. The format only accepts numeric addresses: on 2026-10-12, node0
resolved to 49.190.167.39 and node1 to 51.79.241.169, with no AAAA records returned.
Record those hostname mappings alongside the numeric generation inputs.

Retain other networks' seed arrays, the single Purity mainnet DNS seed and
existing connection behavior. Synchronize requirements, product specification,
discovery architecture/contract, migration report and task index.

Add a failing regression that decodes the actual mainnet BIP155 seed bytes and
compares all addresses and ports before replacing the production list. Verify
generation consistency, preservation of other arrays, build, chain-parameter
tests and the existing fixed-seed functional coverage.

## Validation

Before implementation, `chainparams_tests/purity_mainnet_fixed_seeds` failed
because the compiled list contained 1,306 endpoints instead of 11. After the
replacement, the node/CLI/unit-test build and all 27 cases in `chainparams_tests`
and `net_tests` passed. All four generated arrays match their input files, and
the other three compiled seed arrays match the previous revision byte for byte.

Two isolated mainnet startups with an unreachable proxy verified the runtime
address list through `getnodeaddresses`: `-fixedseeds=0` loaded zero addresses,
and `-fixedseeds=1` loaded exactly the requested 11 addresses and ports.
No actual seed connectivity was tested. `git diff --check` passed.

The complete `feature_config_args.py` functional test passed in 13 seconds.
An earlier isolated `--test_methods test_seed_peers` invocation failed because
it skipped the preceding startup methods that create peers.dat; its fresh
directory therefore logged file creation instead of address loading. That
invocation did not establish a failure in the normal functional test. The
regtest seed arrays and the functional test were not modified. The full
functional suite was not rerun.
