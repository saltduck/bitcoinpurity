# CoinStatsIndex overflow correction

## Boundary and upstream adaptation

Baseline: master `b9a1bfddcae274e02572940c1f6c855f0185a635`. Its CoinStatsIndex
source matches Core v29.4. Reference: [Core #30469](https://github.com/bitcoin/bitcoin/pull/30469),
merge `1861030bea7f55d08173c34d6fa11a16e6eff454`, also present in v30.0.
Core [#32694](https://github.com/bitcoin/bitcoin/pull/32694) moved disk/undo
reads and disconnections into newer BaseIndex interfaces before #30469.
Purity retains CustomRewind, ReverseBlock(CBlock, CBlockIndex*), local
ReadBlockUndo and IsBIP30Unspendable(*pindex); no shared Index API is changed.

## State and storage

Cumulative spent prevouts, non-coinbase outputs and coinbase outputs are
arith_uint256 throughout member state, DBVal and CCoinsStats. Supply-bounded
subsidy, UTXO amount and unspendable constituents remain CAmount. The redundant
unspendable total is removed; it is derived from genesis, BIP30, scripts and
unclaimed rewards. Reward arithmetic uses wide operands before narrowing the
bounded reward difference.

DBVal follows the upstream field order: MuHash digest, output count, bogo size,
UTXO amount, subsidy, three uint256 flow counters, then genesis/BIP30/scripts/
unclaimed amounts. ArithToUint256 and UintToArith256 preserve the fixed 32-byte
representation of each wide counter. A DBVal is 192 bytes; a height record adds
its 32-byte block hash. The old DBVal was 128 bytes and is never opened.

CustomAppend verifies its predecessor against m_current_block_hash. Startup
restores that hash together with all counters and verifies committed MuHash.
ReverseBlock reverses only MuHash operations, skips historical BIP30 coinbases,
checks the prior stored digest and restores every counter from that prior
record. CustomRewind keeps disconnected block records available by hash via
the existing height-to-hash copy. MuHash commits remain atomic with BaseIndex's
best-block locator; unclean startup recovers at the committed point.

## Disabled operation

DEFAULT_COINSTATSINDEX remains false. The unchanged init.cpp conditional is the
only normal startup constructor gate. Directory detection/creation resides in
the constructor, so disabled startup does not inspect legacy index data. No
chainstate, block-index, wallet, validation or chain-selection source is altered.
The widened accounting fields are index-only.

## UTXO statistics snapshot consistency (#34451 / #34908)

The current-tip RPC leaves pindex null; only an explicit height/hash fills it.
Index sync checks guard that pointer. Indexed per-block deltas resolve the
parent from stats.hashBlock, preserving all wide arithmetic and range checks.

ComputeUTXOStats creates a single LevelDB cursor and resolves its block index
from the cursor's best block in one cs_main critical section. The lengthy scan
runs after releasing that locally acquired lock. #34451 first passes this
cursor to the existing bool helper; #34908 subsequently makes the template
construct and return optional<CCoinsStats> and simplifies the public dispatcher.
The public header, hashing, serialization, accumulation and disk-size estimate
stay unchanged. Recursive callers that already hold cs_main retain their lock
throughout the scan, as before (including background AssumeUTXO validation).

The cursor owns its iterator/snapshot throughout the scan. Block index entries
and the selected coins database retain the existing node/chainstate lifetime
assumptions. No additional database or chainstate lifetime protection is added;
this selective backport does not redesign AssumeUTXO chainstate replacement.
The added acquisition uses the same cs_main -> coins DB order as existing
flush/scan callers. No chain-selection or validation implementation changes.
