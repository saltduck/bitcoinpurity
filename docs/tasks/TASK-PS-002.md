# TASK-PS-002: Height-aware signatures and Coinbase commitment

Stage: S2. Status: core implementation and local simulation regression passed.

Requirements: [Purity Signet](../requirements/purity-signet-requirements.md).
Architecture: [implementation contract](../architecture/purity-signet.md).

Test-first implementation; retain Mainnet consensus. Record failing regression,
minimal change and actual validation separately from official release gates.
No official simulated values and no production private keys.

Implemented explicit-height challenge selection, full contextual signatures,
repeat ConnectBlock/disk-read checks, and an internal unsigned-template mode.
Public proposals/submissions cannot select that mode. Full Signet connection
rechecks PoW independently of context-free caches. Header acceptance and index
startup enforce boundary/anchor pins and frozen anchor bits/parent time.

The original output-size checker remains untouched. Only the highest-index
BIP141 Coinbase output with one minimal push, bounded canonical solution,
two direct-pushed DER signatures and an empty solution witness is exempt,
up to 195 bytes. The unit test accepts an exactly 195-byte valid high-S solution
and rejects nonminimal encodings, unrelated payload and other oversized outputs.

Red regressions reproduced old-authority acceptance, missing exception,
old GBT challenge and missing frozen-anchor startup check. Functional tests
passed real inherited blocks 1..10, signed PoW at H/H+1, wrong/missing authority
rejection, ordinary output rejection, a disconnect/reconnect across H with
identical UTXO hash, restart, chainstate reindex and full reindex. Changing even
an unused authority key invalidates the bound challenge and incompatible state.

Additional audit closed raw RPC/REST/P2P/ZMQ bypasses through indexed Signet
reads. Signet reads check ordinary block/merkle/witness structure and signatures
using the index height; Mainnet raw paths retain their original calls. Red
tests reproduced raw-RPC signature and reserved-witness-byte corruption being
served without validation. Both now reject; startup rejects corrupt data even
with cached script status. C++ tests also verify the genuine original block 11
loses authority at H and cached flags cannot bypass connection PoW.
