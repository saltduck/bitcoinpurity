# TASK-013: Purity ASERT discovery service

Status: implemented; focused local verification passed. No production deployment.
Base: local master `501cff308d`; branch `codex/purity-asert-service`.

## Behavior

Experimental `NODE_PURITY_ASERT = (1ULL << 25)` is defined in
`src/protocol.h:351`, advertised through existing local-service paths, and
named `PURITY_ASERT` in RPC. Unknown service names retain their existing behavior.

For automatic non-anchor, non-feeler outbound connections, the first 20 draws
request service preference from AddrMan. A matching record in the requested
network/table takes priority; absent matches use the original bucket selector
immediately. After 20 ineligible draws the original selector is used, preserving
ordinary fallback and the existing 100-draw cap. Preferred selection scans the
existing records (linear cost), retains new/tried balancing, GetChance penalties
and new-table multiplicity, and adds no persistent index or database format.

Existing network-group, desirable-service, recent-attempt, port and addnode
checks remain in the outbound loop. Missing bit 25 is never a disconnect/ban
condition. Existing post-prefix NODE_REDUCED_DATA gating still applies, so
ordinary Core peers do not automatically fill those slots after prefix sync.
Manual connections, anchors, feelers and seed/bootstrap paths are unchanged.

The production height 961637 and pinned hash are unchanged. Header processing
still calls ProcessNewBlockHeaders and applies the consensus hash; bit-25
activation mismatches retain the existing demotion/disconnection behavior.
The new processing changes only add debug logs.

## Changed files

| Files | Change |
| --- | --- |
| src/protocol.h, src/protocol.cpp | Experimental bit definition/documentation and RPC-readable name. |
| src/init.cpp | Include the bit in local services for VERSION and local addresses. |
| src/addrman.h, src/addrman_impl.h, src/addrman.cpp | Optional preferred-services selection with immediate normal fallback if there are no matches. |
| src/net.cpp | Request priority for at most 20 automatic outbound candidates; log preferred connection attempt. |
| src/net_processing.cpp | Debug logs for advertised hints and activation-hash mismatches. |
| src/test/net_tests.cpp | Flag value/names, unknown-bit preservation, address V1/V2 network/disk round trips. |
| src/test/addrman_tests.cpp | Preference, fallback, network/new/tried scoping, last-try metadata, gossip/VERSION service updates and database round trip. |
| src/test/peerman_tests.cpp | Bit 25 is optional before/after activation and cannot supply required Bitcoin services. |
| src/test/denialofservice_tests.cpp | Advertising bit 25 still fails conflicting activation-header validation and loses protection/slots. |
| test/functional/p2p_purity_services.py | Local/pruned VERSION/RPC services, V1/V2 self-address and gossip, peers.dat restart, legacy inbound/outbound/feeler handshakes. |
| test/functional/p2p_node_network_limited.py | Update expected pruned-node local services. |
| test/functional/test_framework/messages.py | Add the test-framework service constant. |
| test/functional/test_runner.py | Register the new functional test for V1 and V2. |
| input/requirements.md, docs/requirements/product-spec.md | Synchronize the discovery requirement without replacing existing DATUM requirements. |
| docs/architecture/purity-peer-discovery.md, docs/api/purity-peer-discovery.md | Selection/storage architecture and P2P/RPC contract. |
| docs/migration-report.md, docs/INDEX.md, docs/tasks/README.md, docs/tasks/TASK-013.md | Migration analysis, indexes and implementation/verification record. |

## Verification

Tests were added before implementation. The initial net test compilation failed
on undefined bit/preference symbols. The subsequent AddrMan preference test
compilation failed on the missing optional preference argument before that
interface was implemented.

Commands run from the worktree:

```sh
cmake -S . -B build -G Ninja -DBUILD_GUI=OFF -DENABLE_WALLET=OFF -DBUILD_DATUM=OFF -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target src/test/CMakeFiles/test_bitcoin.dir/net_tests.cpp.o -j4
cmake --build build --target src/test/CMakeFiles/test_bitcoin.dir/addrman_tests.cpp.o -j2
cmake -S . -B build -DHAVE_DECL_PIPE2=0
cmake --build build --target test_bitcoin bitcoind bitcoin-cli -j8
build/bin/test_bitcoin --run_test=net_tests,addrman_tests,peerman_tests,denialofservice_tests,chainparams_tests --report_level=detailed
python3 build/test/functional/test_runner.py p2p_purity_services.py rpc_net.py p2p_node_network_limited.py p2p_handshake.py p2p_bip110_stale_outbound.py --jobs=4 --tmpdir=/private/tmp/purity-asert-functional-final
python3 -m py_compile test/functional/p2p_purity_services.py test/functional/p2p_node_network_limited.py test/functional/test_framework/messages.py
git diff --check
```

Final build passed; all 63 focused unit cases and all 10 functional cases passed.
The activation security tests use real conflicting PoW headers at shortened test
heights, while chainparams tests separately verify the production height/hash pin.

The initial runtime failed before the tests/node initialized: the SDK detected
macOS-27-only pipe2, absent on this macOS 26.6.2 host. LLDB identified the null
call in existing TokenPipe::Make. HAVE_DECL_PIPE2=0 is a local build-cache override
using the existing pipe fallback; no compatibility source was changed. The
functional runner must be invoked through build/test/functional so it finds the
generated config.ini. The final linker emitted an existing duplicate-library
warning. This verifies local regtest/network behavior; mainnet deployment,
wallet/GUI/DATUM builds and the entire test suite were not run.
