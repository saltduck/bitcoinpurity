```
  BIP: ?
  Layer: Consensus (soft fork)
  Title: Strict Multisig Public Key Validation
  Authors: [Author Name] <[author email]>
  Status: Draft
  Type: Specification
  Assigned: ?
  License: BSD-2-Clause
  Version: 0.1.0
```

## Abstract

This proposal requires every public-key operand consumed by an executed `OP_CHECKMULTISIG` or `OP_CHECKMULTISIGVERIFY` instruction to encode a fully valid secp256k1 public key. Only 33-byte compressed and 65-byte uncompressed public keys are permitted. The existing threshold, matching order, signature-verification algorithm, and signature count are unchanged.

The rule uses two independent block heights: an **UTXO eligibility height** `H_a` and an **enforcement height** `H_b`, with `H_a < H_b`. It is enforced when an input spends an output created at or above `H_a` in a block at or above `H_b`. Outputs created below `H_a` remain permanently exempt. The restriction has no expiry.

The initial deployment target is **Bitcoin Purity**, a Bitcoin-derived chain. This document does not claim activation on Bitcoin's main chain or the assignment of an official BIP number.

## Copyright

This document is made available under the BSD 2-Clause License.

## Motivation

In traditional Bitcoin Script, `OP_CHECKMULTISIG` searches an ordered list of public-key operands for signatures that match the required threshold. Once enough signatures have matched, some public-key operands may remain unexamined. The interpreter's existing public-key encoding checks occur inside the signature-matching loop, rather than unconditionally covering all operands.

Consequently, a public-key operand that the matching loop never visits can contain an arbitrary byte array rather than a public key. An attacker can place many large data pushes into such operands while supplying valid signatures for a smaller number of attacker-controlled keys. In deployments that limit an individual script data push to 256 bytes, multiple such pushes can still make up several kilobytes of nearly contiguous original file bytes, separated only by short script encodings.

Such direct embedding differs from encoding information into mathematical public keys, signatures, or hashes. This proposal targets the former. It does **not** seek to eliminate steganography, prove ownership of every listed private key, identify file formats, or impose a universal limit on all transaction data.

Relay and mining policies that count data-carrying bytes are useful but do not by themselves change block validity. The proposed check is an objective consensus rule: a multisig public-key operand must actually be a valid public key, whether or not it was used to satisfy the threshold.

## Specification

The terms **MUST**, **MUST NOT**, **SHALL**, **SHALL NOT**, **SHOULD**, and **MAY** are to be interpreted as described in RFC 2119.

### 1. Deployment parameters

The deploying chain SHALL define two fixed integer block heights:

- `H_a` — UTXO eligibility height; and
- `H_b` — consensus enforcement height.

These parameters MUST satisfy `0 <= H_a < H_b` and MUST be specified unambiguously before deployment. Their numerical values are **TBD in this draft**. A deployment MUST NOT activate from this draft without defining both heights.

Let `h_s` be the height of the block containing a spending transaction, and let `h_c(i)` be the creation height of the UTXO spent by transaction input `i` in the chain being validated. The new rule applies to input `i` exactly when:

```
strict_multisig_applies(i) = (h_s >= H_b) && (h_c(i) >= H_a)
```

Outputs created **below** `H_a` are exempt at every future spending height. Outputs created **at or above** `H_a` are subject to the new rule when spent **at or above** `H_b`, including outputs created between `H_a` and `H_b`.

The creation height is the block height at which the spent output entered the validated chain, not the transaction's lock time, the script's construction time, the date an address was generated, or the height at which the spending transaction was first relayed. When a parent output is created and spent within the same valid block, its creation height is that block's height.

This is a per-input condition. A transaction spending multiple UTXOs MAY contain both grandfathered and non-grandfathered inputs. A non-grandfathered input MUST pass the new rule irrespective of any grandfathered inputs in the same transaction.

### 2. Affected script operations

For each non-grandfathered input, this rule applies whenever either of the following instructions is **actually executed** in the Script interpreter:

- `OP_CHECKMULTISIG` (`0xae`);
- `OP_CHECKMULTISIGVERIFY` (`0xaf`).

The rule applies in legacy Script (`SigVersion::BASE`) and witness version 0 Script (`SigVersion::WITNESS_V0`), including their applicable P2SH and P2WSH spending paths.

Instructions located in non-executed conditional branches are not subject to this runtime check. No static scan of all script bytes is required. The rule does not apply to Tapscript `OP_CHECKSIGADD`: `OP_CHECKMULTISIG` and `OP_CHECKMULTISIGVERIFY` are already disabled in Tapscript under BIP 342. This proposal does not modify `OP_CHECKSIG` or other signature opcodes.

### 3. Public-key operand validity

Upon executing an affected opcode, the interpreter SHALL perform the existing stack-size and multisig-count checks, establishing:

- `m`: the number of public-key operands, with `0 <= m <= MAX_PUBKEYS_PER_MULTISIG`;
- `n`: the number of supplied signatures, with `0 <= n <= m`; and
- the actual `m` public-key byte strings consumed as operands by that opcode.

For every one of these `m` operands, before the multisig signature-matching loop, the interpreter MUST establish that the operand represents a **fully valid secp256k1 public key** in one of precisely two encodings:

1. **Compressed**: exactly 33 bytes, starting with `0x02` or `0x03`, followed by a 32-byte field element `x` for which the indicated even or odd point `(x, y)` exists on secp256k1.
2. **Uncompressed**: exactly 65 bytes, starting with `0x04`, followed by 32-byte field elements `x` and `y` representing a point on secp256k1.

For both encodings, coordinates MUST be within the field, the point MUST satisfy the secp256k1 curve equation `y^2 = x^3 + 7 (mod p)`, where `p = 2^256 - 2^32 - 977`, and the point at infinity MUST NOT be accepted. Parsing MUST have the same mathematical result as a successful `secp256k1_ec_pubkey_parse` with the relevant encoding.

The following are invalid: wrong lengths; wrong prefixes; hybrid encodings (`0x06` or `0x07`); compressed encodings without a corresponding curve point; and uncompressed encodings with invalid coordinates.

A non-grandfathered input MUST fail consensus script validation if **any** one of the `m` operands fails these checks, regardless of which public keys would have been visited by the original matching algorithm.

This requirement applies when `n = 0`, when the multisig result would otherwise be false, and when a subsequent script instruction would otherwise transform a false result into a successful overall script. It also applies to `OP_CHECKMULTISIGVERIFY`.

Duplicate fully valid public keys remain permitted. No proof of possession of the corresponding private keys is required.

### 4. Signature verification remains unchanged

After the checks in Section 3 succeed, the interpreter MUST execute the pre-existing `OP_CHECKMULTISIG` or `OP_CHECKMULTISIGVERIFY` signature-matching algorithm without modifying:

- the required signature threshold `n`;
- public-key or signature traversal order;
- signature-hash construction;
- ECDSA verification behavior;
- the number of signatures supplied or required;
- `NULLDUMMY`, `NULLFAIL`, or other pre-existing validation rules; or
- the resulting Boolean/script-stack behavior.

In particular, the interpreter MUST NOT require a signature for every public key and MUST NOT perform additional ECDSA signature verifications merely to validate otherwise unused public keys. The new mandatory work consists of public-key parsing and curve-point validation only.

Any public key encountered during the original matching loop is covered by Section 3, including keys whose attempted signature match fails. A public key already checked by the matching loop is not exempt from full curve-point validation.

### 5. Repetition within and across inputs

Every **executed instance** of an affected opcode in a non-grandfathered input MUST independently satisfy Section 3. This includes scripts containing multiple multisig operations, transactions containing multiple inputs, and transactions in which multiple inputs each execute multiple multisig operations.

No new transaction-wide limit on the number of `OP_CHECKMULTISIG` invocations is introduced. Existing script-size, operation-count, signature-operation, stack, and block-weight constraints remain in force.

### 6. Consensus-validating reference algorithm

The following pseudocode is explanatory. The normative requirements are the preceding sections.

```text
For each transaction input i in a block at height h_s:
    coin = UTXO spent by input i
    strict_flag = (h_s >= H_b) AND (coin.creation_height >= H_a)
    execute this input's scripts with strict_flag

When OP_CHECKMULTISIG or OP_CHECKMULTISIGVERIFY executes:
    parse the existing multisig stack arguments
    enforce all existing stack, m, n, and operation-count rules

    if strict_flag:
        for key_bytes in all m public-key operands:
            if not IsFullyValidCompressedOrUncompressedSecp256k1(key_bytes):
                fail script immediately

    execute the original n-of-m ECDSA signature-matching algorithm
    preserve its existing result and stack behavior
```

An implementation MAY use the existing `CPubKey::IsFullyValid()` or an equivalent secp256k1 parser after checking the permitted lengths and prefixes. Implementation-specific script error names, cache layouts, and error strings are not part of consensus.

## Rationale

### Why separate `H_a` from `H_b`?

A single activation height would exempt all UTXOs created before the point of enforcement. That allows an attacker who anticipates the change to create many outputs with hidden, invalid multisig scripts immediately before activation and spend them much later.

With two heights, only outputs created before the earlier `H_a` retain that exemption. Outputs created during the preparation window `[H_a, H_b)` are still spendable under existing rules during that window, but cannot evade the new rule by waiting to be spent after `H_b`.

`H_a` SHOULD be chosen as early as is operationally safe, while `H_b` SHOULD allow sufficient time for node, wallet, and contract software to be reviewed and upgraded. Choosing an `H_a` in the already-mined past can expose previously created UTXOs to a future spending restriction without advance notice. Such a choice requires historical UTXO and script-usage audits; this proposal does not presume that earlier-than-announcement eligibility is harmless.

Hash-committed scripts (including P2SH and P2WSH) need not be disclosed when their funding UTXO is created. Accordingly, nodes generally cannot determine at height `H_a` whether a particular new output will be affected on spending. Wallet and contract software MUST NOT assume that outputs created during the preparation interval are safe to spend after `H_b` without checking their scripts.

### Why validate every operand, including unused ones?

The original multisig algorithm can stop once `n` signatures have matched. Public-key operands not visited by that algorithm are otherwise untyped byte strings. Requiring all operands to encode actual curve points closes this discrepancy without changing the legitimate ability of backup signers to remain inactive.

The check is unconditional once an affected opcode is executed. Restricting it to successful multisig results would permit bypasses using `n = 0` or scripts that deliberately expect a multisig failure and subsequently invert its Boolean result.

### Why retain 65-byte uncompressed keys?

Legacy scripts may legitimately include uncompressed secp256k1 public keys. Permitting fully valid 65-byte points avoids an unnecessary prohibition on those keys. A 65-byte uncompressed public key is not an arbitrary 64-byte file payload: its coordinates are mathematically related and must form a valid curve point. Existing witness-version-0 standardness policies concerning compressed keys remain independent of this consensus rule.

### Why require actual curve validity instead of length and prefix alone?

A 33- or 65-byte array with a plausible prefix is not necessarily a public key. Checking only its length and prefix would still permit intentionally invalid, largely unconstrained data arrays as unused operands. Full curve-point validity makes the rule objectively testable and removes that raw-byte loophole.

This rule is not a proof-of-possession scheme. Encoding information in selected valid elliptic-curve points remains possible; preventing such steganography is expressly outside scope.

### Why no new signatures or global multisig-operation quota?

Additional ECDSA verification of unused keys would change multisig's backup-key semantics and impose unnecessary CPU costs. The new test requires only point parsing, not a signature or discrete-logarithm computation.

Applying the same validity rule at every executed opcode handles one or many multisig operations within any number of inputs. A separate transaction-wide opcode quota would penalize legitimate consolidation or complex scripts without strengthening the requirement that each alleged public key be real.

### Relationship to BIP 110 and relay policy

BIP 110 limits the size of individual pushed elements, not the total size of all data that may be packed into several unused multisig operands. This proposal addresses the latter misuse specifically for public-key operands. It is complementary to, but logically separable from, BIP 110.

Relay policy may separately count suspicious data or reject otherwise consensus-valid data-bearing transactions. Such policy rules are neither required nor sufficient for the consensus property specified here.

## Backward Compatibility

This is a **soft fork on the Bitcoin Purity chain**: after `H_b`, upgraded validators reject some transactions that older validators would accept. Non-upgraded validating nodes and miners must upgrade to follow the new consensus rule.

An output created below `H_a` retains its original multisig validation semantics when spent at any future height. Outputs created at or above `H_a` are **not** guaranteed permanent spendability under the original semantics: scripts relying on malformed or non-curve multisig public-key operands may become unspendable at `H_b` unless they can be redeemed using another valid spending path.

This risk includes presigned transactions, script-hash outputs whose script is revealed only on spending, and users who continue reusing legacy multisig scripts containing invalid operands. The presence of a valid 33-byte or 65-byte public key is not enough unless the point itself is valid.

Existing standard multisig constructions whose listed keys are all real compressed or uncompressed secp256k1 keys, including those with unused backup signers, retain the same n-of-m authorization semantics. Identical-key duplication remains permitted.

The rule changes neither transaction serialization nor addresses, and it does not alter the behavior of scripts in branches that are not executed. Transactions may contain a mixture of grandfathered and non-grandfathered inputs.

Implementations MUST account for chain reorganizations that change UTXO creation heights and for mempool transactions whose outputs are not yet confirmed. Script-validation caches MUST NOT reuse a result validated without the strict rule where the strict rule is now mandatory, or vice versa, unless the cache keys and conditions make that reuse provably safe.

## Security Considerations

### CPU denial of service

All required new cryptographic operations are public-key parsing and curve-point validation, not signature verification. `OP_CHECKMULTISIG` already enforces at most 20 public-key operands per invocation. In legacy and witness-version-0 scripts, the existing 201-operation budget also charges the public-key count for each multisig invocation. Thus, within one script execution, the number of newly parsed multisig operands is bounded by the existing operation budget.

Nevertheless, a malicious block may contain many script executions. Implementations SHOULD benchmark worst-case blocks containing numerous valid and invalid points, duplicate keys, `0-of-m` constructions, and repeated multisig operations. Caching already parsed points is an optional optimization and MUST NOT change observable consensus behavior.

### Historical-UTXO stockpiling

An attacker may stockpile outputs created before `H_a` and spend them after `H_b` under the grandfathered rules. The amount of such future activity is limited by the pre-`H_a` UTXO inventory, not eliminated. Choosing `H_a` early reduces the preparation opportunity but does not justify unexpectedly restricting previously created legitimate outputs.

### Scope and remaining data channels

This proposal prevents non-public-key arbitrary byte strings from being used as multisig public-key operands in affected spends. It does not prevent data encoded into actual public keys, signatures, hashes, ordinary script pushes, dead branches, witness-stack arguments, or other consensus-valid structures. In particular, it is **not** a general transaction data-budget rule and does not by itself guarantee that a block contains no large file payloads.

## Deployment

The deployment is **permanent** on Bitcoin Purity after `H_b`. No expiry or miner-signaling condition is specified. The two hardcoded height parameters are not optional node policy settings.

Before release, the deploying implementation MUST publish concrete mainnet values for `H_a` and `H_b`, provide activation instructions, and audit the potential effect on already-created UTXOs if `H_a` precedes the proposal's public notice. Test networks MAY select different heights for testing. Implementations MAY reject likely noncompliant transactions by policy before `H_b`, but such pre-enforcement policy MUST NOT be confused with consensus invalidity.

**Parameters pending:**

| Parameter | Mainnet value |
| --- | --- |
| `H_a` (UTXO eligibility) | TBD |
| `H_b` (enforcement, `H_b > H_a`) | TBD |
| Expiry | None |
| Chain | Bitcoin Purity |

## Reference Implementation

**Implemented in Bitcoin Purity; network heights not yet assigned.** The rule is dormant on every public network until `H_a` and `H_b` are set. Regtest enables it with `-testactivationheight=strictmultisigutxo@H_a -testactivationheight=strictmultisig@H_b`.

Implementation points:

- `SCRIPT_VERIFY_STRICT_MULTISIG_PUBKEYS` and `SCRIPT_ERR_MULTISIG_PUBKEY` in `src/script/interpreter.h` and `src/script/script_error.h`; the full-operand check in the shared `OP_CHECKMULTISIG(VERIFY)` case of `src/script/interpreter.cpp`.
- `Consensus::Params::nStrictMultisigUtxoHeight` (`H_a`) and `nStrictMultisigEnforcementHeight` (`H_b`) in `src/consensus/params.h`.
- Block flag at `H_b` in `GetBlockScriptFlags` and per-input relaxation for coins below `H_a` in `Chainstate::ConnectBlock` (`src/validation.cpp`).
- Relay-policy enforcement through `STANDARD_SCRIPT_VERIFY_FLAGS` (`src/policy/policy.h`).
- Tests in `src/test/strict_multisig_tests.cpp`.

The implementation was expected to:

1. Define the two fixed heights in its chain consensus parameters.
2. Introduce a distinct mandatory script-verification flag, for example `SCRIPT_VERIFY_STRICT_MULTISIG_PUBKEYS`.
3. Activate the flag at `H_b`, with per-input relaxation for coins created below `H_a`, using the existing UTXO-height-aware validation mechanism.
4. Add the unconditional, full-point-validation loop to both affected opcodes' shared interpreter case after stack/count validation and before ECDSA matching.
5. Preserve existing legacy/P2SH/P2WSH checks and signing semantics.
6. Handle mempool, same-block parent/child spends, reorgs, cached execution results, and block connection consistently.

Relevant pre-change implementation references:

- [Bitcoin Purity `src/script/interpreter.cpp`](https://github.com/saltduck/bitcoinpurity/blob/83dfae591af3d1dfd7ccd803516279f3b72460b0/src/script/interpreter.cpp)
- [Bitcoin Purity `src/validation.cpp`](https://github.com/saltduck/bitcoinpurity/blob/83dfae591af3d1dfd7ccd803516279f3b72460b0/src/validation.cpp)
- [Bitcoin Purity `src/pubkey.cpp`](https://github.com/saltduck/bitcoinpurity/blob/83dfae591af3d1dfd7ccd803516279f3b72460b0/src/pubkey.cpp)

## Test Vectors

**Implementation-verified hexadecimal transaction and block vectors have not yet been produced.** The following are normative-behavior test scenarios to be implemented on regtest with real UTXOs, valid signatures where appropriate, and block submission in addition to mempool checks. Except where noted, the tests below assume a spending height `h_s >= H_b`, a spent output created at or above `H_a`, and satisfaction of all otherwise applicable consensus rules.

| ID | Scenario | Expected result |
| --- | --- | --- |
| T01 | 3-of-15 multisig: three valid signing keys; twelve unused 256-byte arbitrary data operands | Reject |
| T02 | Same construction, but spent UTXO created at `H_a - 1` | Accept under pre-existing rules |
| T03 | Same construction, UTXO created at `H_a`, spent at `H_b - 1` | Accept under pre-existing rules |
| T04 | 3-of-15 with fifteen valid compressed curve points and three valid signatures | Accept |
| T05 | Valid uncompressed 65-byte keys with prefix `0x04` in legacy/P2SH multisig | Accept |
| T06 | One unused operand has length 32, 64, or 256 bytes | Reject |
| T07 | One unused operand has 33 bytes and `0x02`/`0x03` prefix but invalid curve coordinates | Reject |
| T08 | One unused operand has 65 bytes and `0x04` prefix but invalid curve coordinates | Reject |
| T09 | One unused operand is a valid-looking 65-byte hybrid (`0x06`/`0x07`) encoding | Reject |
| T10 | `0-of-m` executed with an invalid public-key operand; no ECDSA signatures requested | Reject |
| T11 | Multisig produces false and a subsequent `OP_NOT` would produce true, but one public-key operand is invalid | Reject |
| T12 | Executed `OP_CHECKMULTISIGVERIFY` with an invalid operand | Reject |
| T13 | Multisig instruction occurs only in a non-executed branch | New rule imposes no check |
| T14 | Two multisig operations execute in one input, and the second contains an invalid operand | Reject |
| T15 | Two inputs in one transaction: first UTXO created below `H_a`, second at/above `H_a`; only the first has invalid operands | No rejection from this new rule |
| T16 | Same as T15, but only the second input has invalid operands | Reject |
| T17 | Multiple identical but fully valid public keys in an executed multisig operation | No rejection from this new rule |
| T18 | UTXO created and spent in the same block at/above `H_b`, and its multisig has an invalid operand | Reject |
| T19 | A reorganization changes the relevant coin's creation height across `H_a` | Re-evaluate according to the new active-chain height |
| T20 | Valid 65-byte uncompressed key in P2WSH | No rejection from this rule; other consensus/policy rules remain applicable |

Vectors MUST additionally exercise boundaries `H_a - 1`, `H_a`, `H_b - 1`, and `H_b`; both opcodes; 0, 1, and 20 public-key operands; mixed-input flag handling; script caches; and an attempted policy bypass via direct block submission. A test passed by `testmempoolaccept` alone is not evidence of block-level consensus validity.

## References

- [RFC 2119: Key words for use in RFCs to Indicate Requirement Levels](https://www.rfc-editor.org/rfc/rfc2119)
- [BIP 3: Updated BIP Process](https://bips.dev/3/)
- [BIP 16: Pay to Script Hash](https://bips.dev/16/)
- [BIP 141: Segregated Witness](https://bips.dev/141/)
- [BIP 342: Validation of Taproot Scripts](https://bips.dev/342/)
- [BIP 110: Reduced Data Temporary Softfork](https://bips.dev/110/)
- [Bitcoin Purity consensus documentation](https://github.com/saltduck/bitcoinpurity/blob/83dfae591af3d1dfd7ccd803516279f3b72460b0/doc/purity-consensus.md)
- [Bitcoin Knots PR #422: count unproven pubkeys as datacarrier bytes](https://github.com/bitcoinknots/bitcoin/pull/422) (relay policy, not this consensus rule)

## Open Deployment Items

The following items MUST be resolved before deployment and do not change the technical rules stated above:

1. Assign `H_a` and `H_b` for the intended network.
2. Identify submitting author(s) and contact address(es); obtain any formal proposal number, if applicable.
3. Publish an implementation and block-validated machine-readable test vectors.
4. Complete a UTXO/script compatibility audit, especially if `H_a` is set in the past.
5. Benchmark adversarial curve parsing and validate script-cache/reorganization behavior.
