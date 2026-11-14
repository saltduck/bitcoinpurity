# Core v32.1 IPC contract migration

Purity uses the Bitcoin Core v32.1 Cap'n Proto IPC wire contract, reference commit
`f0de87f040621468b956f01f526a43a010310501`. This replaces the earlier Purity/Knots
layout on the existing socket. It is an intentional compatibility break, not an
additional socket or a version negotiated through Stratum V2.

## Client requirements

Regenerate bindings from the current `src/ipc/capnp/` schemas and use the matching
libmultiprocess context/thread-map protocol. The same schema ID does not make old
method numbers or parameter layouts compatible. The build dependency is the
unmodified libmultiprocess snapshot shipped in the fixed Core v32.1 archive;
`contrib/devtools/build-ipc-deps.sh` checks its archive checksum.

| Interface | Current contract |
| --- | --- |
| Init | `makeMining @3`, `makeRpc @4`; old `makeMiningOld2 @2` explicitly fails. |
| Mining | `createNewBlock @4` has context, options and `cooldown=true`; `checkBlock @5`; `interrupt @6`; `submitBlock @7`; txid/wtxid lookup at `@8`/`@9`. |
| BlockTemplate | Structured `getCoinbaseTx @5`; merkle path `@6`; `waitNext @8`; `interruptWait @9`; `submitSolution @10`. Old `submitSolutionOld7 @7` explicitly fails. |
| Rpc | `executeRpc @0` forwards JSON-RPC requests, URI and user identity. |
| Echo | Echo/debug interface; no mining responsibilities. |

`BlockCreateOptions` exposes Core's three fields: `useMempool`,
`blockReservedWeight` and `coinbaseOutputMaxAdditionalSigops`, with Core defaults.
Purity-specific assembler options and coin-age getters remain in-process and are
not additional wire fields or methods. Fees and sigops returned by BlockTemplate
exclude coinbase. Missing transaction lookups retain request order with empty
Data entries representing missing transactions. Structured coinbase fields remain
the original template metadata after `submitSolution` updates the stored block.

`submitSolution` and `submitBlock` return `result`, `reason` and `debug`. A true
result means a newly accepted block; duplicates return false with `duplicate`.
Supply the complete coinbase witness. Validation uses Purity consensus, including
ASERT/RDTS and version constraints; protocol alignment does not select BTC rules.

Use `cooldown=false` for isolated regtest or a single-miner Signet where waiting
for synchronization could stall. The embedded SV2 gateway still supports only
mainnet/regtest; this IPC option does not add SV2 Signet support. `waitNext`
supports timeout, fee threshold, parent changes and explicit interruption.
The listener starts after chainstate initialization. Shutdown disconnects external
clients while retaining the controlling parent connection until normal teardown.

## CKPool follow-up

The reviewed CKPool snapshot contains Core v31.0-era bindings. CKPool maintainers
must update their own schema, generated code and call sites to v32.1, including:

1. Obtain Mining through Init `@3` and pass a worker context where required.
2. Call `submitSolution @10`, consume rejection details and handle duplicate failure.
3. Retire the old `@7` submit call; it returns an explicit upgrade error.
4. Use structured coinbase and Core list/default semantics.
5. Run an isolated Mining/JDS/IPC validation test against Purity, using actual V1
   PoW, missing transactions, difficulty changes, reorgs and single-side failures.

No CKPool or SRI source is changed by this migration. Matching schemas alone is
not interoperability acceptance. Native Windows and production pool acceptance
remain separate gates; PR #44 remains draft. See
[TASK-038](../docs/tasks/TASK-038.md) and [TASK-037](../docs/tasks/TASK-037.md).
