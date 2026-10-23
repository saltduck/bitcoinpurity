> Historical review: H=318621 below has been superseded by late selection
> H=T+1 immediately before mining. Previous test counts refer to that earlier
> source snapshot. See [late-H review](purity-signet-late-h-review.md) for the
> new preparation/conversion implementation and its own validation evidence.

# Signet change review (2026-10-15)

Review baseline: `cc85cfad90dea5447ff313f3158147ebb7bf1b47`. Scope includes all
tracked changes and new implementation, tests and configuration in this change.
All work and validation are local; no commit, PR or deployment was created.

Latest owner decisions: H=318621, default P2P/RPC ports 38333/38332, and the
`signet/` directory. Keep `ChainType::SIGNET`, `[signet]` and RPC `chain=signet`.
There is no additional Purity mode.

## Conclusion and Mainnet isolation

Within the reviewed code and regression scope, no new path was found that
changes Mainnet consensus acceptance results. The following match the baseline
byte for byte: `CMainParams`, `CalculateASERT()`, `GetNextASERTWorkRequired()`
and the entire `src/consensus/tx_verify.cpp` file. The only added dispatch in
`GetNextWorkRequired()` is guarded by `params.signet_blocks` and reached only
after permanent activation. Mainnet retains its original 224-bit assertion,
formula, truncation and validation path. This conclusion does not equate
simulation parameters, short-history tests or fuzzing with official release
acceptance.

| Path | Review result |
| --- | --- |
| `CreateChainParams` / `ReadSigNetArgs` in `src/chainparams.cpp` | JSON is read only when SIGNET is selected. MAIN construction does not read it, including missing or malformed files. Configuration cannot override genesis, magic, powLimit or the 600/86400 timing constants. |
| `src/chainparamsbase.cpp` / `src/kernel/chainparams.h,cpp` | Default ports and directory now match Bitcoin Signet; configuration can override ports. Mainnet retains its original datadir and RPC port 8332. |
| Parameter interaction, historical seeds and ZMQ in `src/init.cpp` | New behavior is guarded by SIGNET. Mainnet help uses a Signet factory that does not read JSON, solely to generate option descriptions. |
| `src/signet.cpp` | Explicit height selects the challenge. Original BIP325 transaction construction and verification flags are retained. At H and later, commitment encoding and both signatures are strictly checked. |
| Contextual validation / ConnectBlock / index loading in `src/validation.cpp` | Height comes from the parent index, not a block claim. `fChecked` cannot bypass signatures. Activation, parent and anchor pins, anchor bits and parent time are checked. |
| `src/node/blockstorage.cpp` and RPC/REST/P2P/ZMQ block reads | SIGNET reads use index height and recheck merkle, witness and signatures. Mainnet retains the original raw-read calls. |
| Coinbase RDTS | Ordinary `CheckOutputSizes()` is unchanged. Only SIGNET at H or later receives the exception, only for the highest-index output actually selected by BIP141, after valid signatures, with a 195-byte maximum. Other outputs retain the original checks. |
| Unsigned templates | Only the internal BlockAssembler supplies this mode. Actual ConnectBlock disallows it. Public proposal / submitblock cannot select it. Mainnet is unaffected by this Signet-specific mode. |
| `src/pow.cpp` | The original Mainnet algorithm is unchanged. Signet uses a separate wide-target entry with the same integer formula. H may claim a harder target; H+1 onward requires exact bits. |
| `src/headerssync.cpp` / `src/net_processing.cpp` | New pins, prefix retention and peer qualification apply only to SIGNET. Expected old-chain conflicts do not add bans. Qualification requires valid block evidence; service flags alone do not grant it. |
| Independent miner | Checks node configuration, template challenge and bits; obtains quorum from two RPC wallets; changes only the nonce after signing; checks real work, stale tips and submission results. |
| Test framework and wallet-directory scanning | The mappings and scan changes previously added for puritysignet/ were removed, restoring the baseline. Clients with the same network-directory name run concurrently under separate base datadirs. |

SHA256 fingerprints and comparisons for the original 224-bit path are recorded
in `build/signet-review-mainnet-proof.json`. The ASERT polynomial's maximum sum
is 18446563080438344768, below UINT64_MAX; the maximum factor is 131071, below
2^17. Therefore a reference target below 2^239 produces a multiplication result
strictly below 2^256; the actual Signet limit is only 234 bits. Left-shift
overflow is detected and clamped to powLimit using the original formula;
underflow is clamped to 1. Actual chain-height and uint32 timestamp ranges do
not overflow the int64 exponent calculation. Mainnet's 224-bit assertion was
not relaxed.

## Configuration and verified historical parameters

The [launch JSON](../contrib/purity-signet/signet-params.json) uses the
owner-selected H. [simulation.json](../contrib/purity-signet/simulation.json)
retains H=11 and anchor=6 exclusively for fast regressions using ten real
historical blocks; these are not the official fork height.

| Parameter | Current value and evidence |
| --- | --- |
| H | 318621, selected by the owner |
| H-1 | 318620, `0000000a573b25aeae051a91769714a6c384753bedba235a334a5196a2a2af15` |
| Anchor | H-5=318616, `0000000ea43e56e8ccdc528e59092db2a83221bcfd8819f64eea5f40ddb05844` |
| Anchor parent | 318615, `000000136a72d8c42770f26ff1e6ba148d4974c1533ebf0bd62d5a75ef4828bf` |
| Anchor bits / parent time | `1d13c0c2` / 1787261860 |
| Required bits for the first H block | `1d13fbb5`; independent Python integer computation agrees with the C++ regression. H may claim a harder valid target. |
| Challenge public keys | Three public test-fixture keys; authority remains simulated |
| First Purity block hash at H | `null`, allowed only with simulation=true; freeze after the actual first block is produced and verified |
| minimum_chain_work | Provisionally retains the lower original Signet baseline threshold; it is not asserted to be height 318620's cumulative chainwork |
| Ports / directory | Defaults 38333/38332; signet/ under the base datadir |
| Seeds | .invalid / loopback values are simulated; historical seed endpoints are configured separately |

Six real blocks at heights 318615 through 318620 were downloaded and checked
for header SHA256d/PoW, parent linkage, transaction merkle root, witness
commitment and original 1-of-2 BIP325 ECDSA signatures. The
[raw-header records](signet-318621-headers.json) and
[block-validation summary](signet-318621-block-validation.json) preserve public
sources, raw SHA256 and results. Verification used independent Python
signature reconstruction and a secp256k1 verifier; raw files and the executing
script are retained in the local ignored build/ directory. These six checks
from one public source do not replace full history, transaction-script and
UTXO validation for 0 through 318620, or confirmation by a second independently
operated node.

## Findings and actions

1. Missing configuration field: with simulation=true, omitted activation_hash
   was accepted because UniValue treated the missing value as explicit null.
   A new regression reproduced this; all schema fields must now be explicit.
   Explicit null remains available for the simulated first-block workflow.
   This fix affects only the Signet JSON reader.
2. Complete coverage of owner amendments: the RPC fallback member still used
   58332; it now uses 38332. Regressions cover both base parameters and the
   kernel fallback. Original directory mappings were restored; no old state
   is automatically moved, deleted or copied.
3. Malformed duplicate blocks and peer qualification: the initial new test
   failed because the connection was rejected. Inspection confirmed that the
   existing P2P `IsBlockMutated` check already rejects invalid witness data.
   The test now checks the actual rejection behavior; no redundant peer check
   or production change was added to this path.
4. Existing old-chain state: handing original Core's post-H signet/ index and
   chainstate directly to this client fails startup validation instead of
   continuing under the original signing authority. The Core 28.0 comparison
   regression verified this with the small H=11 fixture. Existing state with
   historical or side-branch indexes conflicting with pins may also fail.
   Back up first, then revalidate verified raw history; files are not destroyed
   automatically.
5. Pre-existing CoinStats test defect: the diff contains a test-only cursor-lock
   repair independent of Signet. Production CoinStats/UTXO code is unchanged.
   The cause and original-baseline reproduction are recorded in the
   [acceptance record](purity-signet-acceptance.md).

## Validation record

- Final configuration/pow/signature/validation/headers/DoS unit run: 69 cases
  passed.
- Full current macOS CTest run: 150/150 passed.
- Five Signet/independent-miner/Mainnet-mining functional tests passed,
  including duplicate-witness rejection.
- Original Core 28.0 comparison using the H=11 fixture passed: shared 0 through
  10 prefix and UTXOs; identical network-directory names under separate base
  datadirs; distinct H continuations; activation-pin restart; rejection of
  an old-chain post-H database at startup.
- Linux arm64 / Ubuntu Clang 18.1.3 with libFuzzer + ASan + UBSan: both targets
  exited 0. signet executed 1,907,931 inputs in 181 seconds; signet_asert
  executed 4,202,403 inputs in 181 seconds. Total: 6,110,334 executions, with
  no sanitizer errors or assertion failures; peak RSS was 344/595 MiB.
  [Structured results](purity-signet-fuzz-results.json) preserve parameters,
  source fingerprints and the binary SHA256. Signet seeds include original
  blocks 1 through 11, a newly signed block and malformed variants. ASERT
  covers 234/239-bit limits, saturation/underflow and equality with the original
  algorithm on the Mainnet domain.

Actual logs are in `build/signet-review-*.log` and `build/linux-fuzz-review/`.
Earlier evidence remains available: Linux CTest 150/150, 79 cross-architecture
ASERT vectors, 82 UBSan cases and the real-time 600-second interval. Those runs
predate the latest default-port/directory changes and do not replace the new
regressions. The ordinary PoW wrapper has a simplified fuzz-mode branch;
fuzzing does not claim real mining proof. Real PoW evidence comes from node
functional tests, raw-header checks and the earlier independent-miner tests.

## Remaining official acceptance and risks

- H is fixed; three real authorities, operated seeds and the first Purity block
  hash remain unfrozen. simulation=true and known test authorities must retain
  explicit labels and must not be published as official configuration.
- H is an existing old-chain height. If the first Purity timestamp is much later
  than H-1, subsequent ASERT difficulty eases substantially under the original
  formula and may reach powLimit. The miner's 600-second scheduling does not
  automatically eliminate the historical time gap. This is a consequence of
  the selected historical height and operation; this change did not move H,
  falsify anchor time, compensate the formula or modify Mainnet. Official
  first-block time and H/H+1 templates need separate records.
- Full historical hashes, transaction scripts and UTXOs for 0 through 318620,
  H-1 cumulative work and independent-node comparison remain operational gates.
  Current evidence covers the source inheritance path, the ten-block fixture
  and six actual boundary blocks.
- Default ports and directory names are identical. Concurrent clients on one
  host need separate base datadirs and a port override for one process. Old
  wallets, cookies and addrman are not automatically isolated. Existing
  old-chain post-H state requires backup and explicit revalidation.
- Shared magic and address formats retain transaction-replay and address-cache
  mixing risks. Block evidence shows that a peer can supply valid chain data;
  it does not authenticate identity. Malicious peers can relay public valid
  blocks. Stale/timeout management and connections across multiple network
  groups still require public-network operational validation.
- Bounded fuzzing does not cover every input or constitute a formal consensus
  proof. Mainnet isolation relies jointly on source comparison, guarded
  branches and actual Mainnet regressions.
