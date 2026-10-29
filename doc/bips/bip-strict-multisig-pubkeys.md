```
  BIP: ?
  Layer: Consensus (soft fork)
  Title: Strict Multisig Public Keys and Minimum Threshold
  Authors: [Author Name] <[author email]>
  Status: Draft
  Type: Specification
  Assigned: ?
  License: BSD-2-Clause
  Version: 0.2.0
```

## Abstract

This proposal requires every public-key operand consumed by an executed `OP_CHECKMULTISIG` or `OP_CHECKMULTISIGVERIFY` instruction to encode a fully valid secp256k1 public key. Only 33-byte compressed and 65-byte uncompressed public keys are permitted. It also requires the multisignature threshold `n` and number of public-key operands `m` to satisfy `1 <= n <= m <= 20` and `3 * n >= m` (equivalently, `n >= ceil(m / 3)`). The signature-matching order, ECDSA verification algorithm, and existing rules for producing and checking the `n` signatures remain unchanged.

The rule uses two independent block heights: an **UTXO eligibility height** `H_a` and an **enforcement height** `H_b`, with `H_a < H_b`. It is enforced when an input spends an output created at or above `H_a` in a block at or above `H_b`. Outputs created below `H_a` remain permanently exempt. The restriction has no expiry.

The initial deployment target is **Bitcoin Purity**, a Bitcoin-derived chain. This document does not claim activation on Bitcoin's main chain or the assignment of an official BIP number.

## Copyright

This document is made available under the BSD 2-Clause License.

## Motivation

In traditional Bitcoin Script, `OP_CHECKMULTISIG` searches an ordered list of public-key operands for signatures that match the required threshold. Once enough signatures have matched, some public-key operands may remain unexamined. The interpreter's existing public-key encoding checks occur inside the signature-matching loop, rather than unconditionally covering all operands.

Consequently, a public-key operand that the matching loop never visits can contain an arbitrary byte array rather than a public key. An attacker can place many large data pushes into such operands while supplying valid signatures for a smaller number of attacker-controlled keys. In deployments that limit an individual script data push to 256 bytes, multiple such pushes can still make up several kilobytes of nearly contiguous original file bytes, separated only by short script encodings.

Such direct embedding differs from encoding information into mathematical public keys, signatures, or hashes. This proposal targets the former. It does **not** seek to eliminate steganography, prove ownership of every listed private key, identify file formats, or impose a universal limit on all transaction data.

Relay and mining policies that count data-carrying bytes are useful but do not by themselves change block validity. The proposed checks are objective consensus rules: a multisig public-key operand must actually be a valid public key, whether or not it was used to satisfy the threshold, and a multisig operation must meet a minimum one-third signature threshold. The minimum-threshold constraint is an additional restriction on spending-policy expressiveness; it is not required for full public-key validation and its incremental data-reduction benefit and compatibility cost require separate evaluation.

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

### 3. Minimum threshold and public-key operand validity

Upon executing an affected opcode, the interpreter SHALL perform the existing stack-size and multisig-count checks, establishing:

- `m`: the number of public-key operands, with the existing range `0 <= m <= MAX_PUBKEYS_PER_MULTISIG`;
- `n`: the number of supplied signatures, with the existing range `0 <= n <= m`; and
- the actual `m` public-key byte strings consumed as operands by that opcode.

For an input to which this proposal applies, every executed affected opcode MUST additionally satisfy both of the following consensus conditions:

```
1 <= n <= m <= 20
3 * n >= m
```

The second condition is equivalent to `n >= ceil(m / 3)` and MUST be evaluated using exact integer arithmetic. The input MUST fail consensus script validation when either condition fails, even if the opcode would otherwise return true, return false and be inverted by a later opcode, or be otherwise neutralized by the enclosing script. In particular, `0-of-m`, including `0-of-0`, is invalid under this rule. `n` is the number of signatures required by the currently executed multisig operation, **not** the count of distinct public keys or private-key holders. The existing interpretation of stack-supplied `n` and `m` is unchanged; this restriction applies regardless of whether those values were supplied by static `OP_N` instructions or computed by other script operations.

After the existing argument and operation-count checks, and the additional minimum-threshold check, but before the multisig signature-matching loop, the interpreter MUST establish that **every one** of the `m` operands represents a **fully valid secp256k1 public key** in one of precisely two encodings:

1. **Compressed**: exactly 33 bytes, starting with `0x02` or `0x03`, followed by a 32-byte field element `x` for which the indicated even or odd point `(x, y)` exists on secp256k1.
2. **Uncompressed**: exactly 65 bytes, starting with `0x04`, followed by 32-byte field elements `x` and `y` representing a point on secp256k1.

For both encodings, coordinates MUST be within the field, the point MUST satisfy the secp256k1 curve equation `y^2 = x^3 + 7 (mod p)`, where `p = 2^256 - 2^32 - 977`, and the point at infinity MUST NOT be accepted. Parsing MUST have the same mathematical result as a successful `secp256k1_ec_pubkey_parse` with the relevant encoding.

The following are invalid: wrong lengths; wrong prefixes; hybrid encodings (`0x06` or `0x07`); compressed encodings without a corresponding curve point; and uncompressed encodings with invalid coordinates.

A non-grandfathered input MUST fail consensus script validation if **any** one of the `m` operands fails these checks, regardless of which public keys would have been visited by the original matching algorithm.

Both the minimum-threshold and public-key-validity checks apply regardless of whether the multisig result would otherwise be true or false, including when a subsequent script instruction would otherwise transform a false result into a successful overall script. They also apply to `OP_CHECKMULTISIGVERIFY`. For `n = 0`, the minimum-threshold check itself mandates rejection.

Duplicate fully valid public keys remain permitted. No proof of possession of the corresponding private keys is required.

### 4. Signature verification remains unchanged

After the additional threshold and public-key-validity checks in Section 3 succeed, the interpreter MUST execute the pre-existing `OP_CHECKMULTISIG` or `OP_CHECKMULTISIGVERIFY` signature-matching algorithm without modifying:

- the threshold `n` supplied to the opcode (subject to Section 3's new lower bound);
- public-key or signature traversal order;
- signature-hash construction;
- ECDSA verification behavior;
- the number of signatures processed by the original matching algorithm for the permitted value of `n`;
- `NULLDUMMY`, `NULLFAIL`, or other pre-existing validation rules; or
- the resulting Boolean/script-stack behavior.

In particular, the interpreter MUST NOT require a signature for every public key and MUST NOT perform additional ECDSA signature verifications merely to validate otherwise unused public keys. The new mandatory work consists of a constant-time integer threshold comparison plus public-key parsing and curve-point validation only. The required `n` signatures continue to be checked under the original algorithm; this proposal does not require distinct public keys or distinct signers.

Any public key encountered during the original matching loop is covered by Section 3, including keys whose attempted signature match fails. A public key already checked by the matching loop is not exempt from full curve-point validation.

### 5. Repetition within and across inputs

Every **executed instance** of an affected opcode in a non-grandfathered input MUST independently satisfy **both** conditions in Section 3: the minimum-threshold relation and full validity of every public-key operand. This includes scripts containing multiple multisig operations, transactions containing multiple inputs, and transactions in which multiple inputs each execute multiple multisig operations. Validity of one multisig operation does not exempt any other operation in the same input or transaction.

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
        if not (1 <= n <= m <= 20 and 3 * n >= m):
            fail script immediately
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

The checks are unconditional once an affected opcode is executed. Restricting them to successful multisig results would permit bypasses using scripts that deliberately expect a multisig failure and subsequently invert its Boolean result; the new threshold condition independently rules out `n = 0`.

### Why require at least one-third of the public keys as signatures?

The added requirement `3 * n >= m` preserves common low-threshold multisig arrangements such as 1-of-3, 2-of-5, 2-of-6, 3-of-9, 5-of-15, and 7-of-20 while disallowing particularly imbalanced arrangements such as 1-of-4, 2-of-7, 3-of-10, and 3-of-15. The intent is to limit the number of unused public-key operands relative to the required signature count, without imposing a requirement that every public key sign.

For the maximum `m = 20`, the lowest permitted threshold becomes `n = 7`, leaving at most 13 non-signing public-key operands instead of the 19 possible under 1-of-20. In a 3-of-15 example, the lowest permitted threshold becomes 5-of-15. These counts concern the public-key slots left unused by the threshold, not guaranteed independently usable file-data capacity: full curve-point validity is a separate, stricter condition.

The one-third ratio is a **design choice**, not a cryptographic security theorem or a proof of an optimal data limit. In particular, valid repeated public keys and repeated matching signatures remain allowed: increasing `n` does not necessarily require `n` distinct private keys or proportionally more signing work. It also does not impose any transaction-wide or block-wide total-data limit. The incremental deterrence provided by this ratio beyond full public-key validation MUST be assessed against the loss of legitimate low-threshold multisig arrangements before `H_a` and `H_b` are finalized.

Alternatives considered include `n >= 1` alone, `n >= ceil(m / 2)`, and a fixed cap such as `m - n <= 2`. The first has little effect on the unused-key share, while the latter two prohibit a larger set of legitimate low-threshold and backup-signer arrangements. The one-third rule is a provisional balance chosen for this draft; it is normatively specified here so implementations and test vectors can evaluate a concrete rule.

### Why retain 65-byte uncompressed keys?

Legacy scripts may legitimately include uncompressed secp256k1 public keys. Permitting fully valid 65-byte points avoids an unnecessary prohibition on those keys. A 65-byte uncompressed public key is not an arbitrary 64-byte file payload: its coordinates are mathematically related and must form a valid curve point. Existing witness-version-0 standardness policies concerning compressed keys remain independent of this consensus rule.

### Why require actual curve validity instead of length and prefix alone?

A 33- or 65-byte array with a plausible prefix is not necessarily a public key. Checking only its length and prefix would still permit intentionally invalid, largely unconstrained data arrays as unused operands. Full curve-point validity makes the rule objectively testable and removes that raw-byte loophole.

This rule is not a proof-of-possession scheme. Encoding information in selected valid elliptic-curve points remains possible; preventing such steganography is expressly outside scope. In particular, an encoder may preserve much of an intended payload in a compressed point's x-coordinate while searching a small number of other bytes for a valid point. Whether such lightly modified bytes constitute sufficiently "nearly contiguous" file storage under the project's data-abuse objective is an unresolved threat-model question, not a protection guaranteed by the present checks.

### Why no new signatures or global multisig-operation quota?

Additional ECDSA verification of unused keys would change multisig's backup-key semantics and impose unnecessary CPU costs. The new test requires only point parsing, not a signature or discrete-logarithm computation.

Applying the same validity rule at every executed opcode handles one or many multisig operations within any number of inputs. A separate transaction-wide opcode quota would penalize legitimate consolidation or complex scripts without strengthening the requirement that each alleged public key be real.

### Relationship to BIP 110 and relay policy

BIP 110 limits the size of individual pushed elements, not the total size of all data that may be packed into several unused multisig operands. This proposal addresses the latter misuse specifically for public-key operands. It is complementary to, but logically separable from, BIP 110.

Relay policy may separately count suspicious data or reject otherwise consensus-valid data-bearing transactions. Such policy rules are neither required nor sufficient for the consensus property specified here.

## Backward Compatibility

This is a **soft fork on the Bitcoin Purity chain**: after `H_b`, upgraded validators reject some transactions that older validators would accept. Non-upgraded validating nodes and miners must upgrade to follow the new consensus rule.

An output created below `H_a` retains its original multisig validation semantics when spent at any future height. Outputs created at or above `H_a` are **not** guaranteed permanent spendability under the original semantics: scripts relying on malformed or non-curve multisig public-key operands **or on a multisig threshold below one-third** may become unspendable at `H_b` unless they can be redeemed using another valid spending path.

This risk includes presigned transactions, script-hash outputs whose script is revealed only on spending, and users who continue reusing legacy multisig scripts containing invalid operands or disallowed `n:m` ratios. A valid public-key curve point does not compensate for a threshold below `ceil(m / 3)`; a compliant threshold does not compensate for an invalid public key. This compatibility impact is broader than the public-key-validation requirement alone.

Existing multisig constructions whose listed keys are all real compressed or uncompressed secp256k1 keys **and whose threshold satisfies `3 * n >= m`** retain the same n-of-m authorization semantics, including unused backup signers. Otherwise-valid multisig constructions with lower thresholds, such as 1-of-4, 2-of-7, 3-of-10, and 3-of-15, become invalid for non-grandfathered inputs after `H_b`, irrespective of whether their public keys are fully valid. Identical-key duplication remains permitted.

The rule changes neither transaction serialization nor addresses, and it does not alter the behavior of scripts in branches that are not executed. Transactions may contain a mixture of grandfathered and non-grandfathered inputs.

Implementations MUST account for chain reorganizations that change UTXO creation heights and for mempool transactions whose outputs are not yet confirmed. Script-validation caches MUST NOT reuse a result validated without the strict rule where the strict rule is now mandatory, or vice versa, unless the cache keys and conditions make that reuse provably safe.

## Security Considerations

### CPU denial of service

All required new cryptographic operations are public-key parsing and curve-point validation, not signature verification. `OP_CHECKMULTISIG` already enforces at most 20 public-key operands per invocation. In legacy and witness-version-0 scripts, the existing 201-operation budget also charges the public-key count for each multisig invocation. Thus, within one script execution, the number of newly parsed multisig operands is bounded by the existing operation budget.

The new `3 * n >= m` comparison is inexpensive and MUST NOT introduce additional ECDSA verification. It does not guarantee increased computational costs for the transaction creator because repeated valid keys and signatures remain permitted. Nevertheless, a malicious block may contain many script executions. Implementations SHOULD benchmark worst-case blocks containing numerous valid and invalid points, duplicate keys, rejected `0-of-m` constructions, boundary-ratio multisigs, and repeated multisig operations. Caching already parsed points is an optional optimization and MUST NOT change observable consensus behavior.

### Historical-UTXO stockpiling

An attacker may stockpile outputs created before `H_a` and spend them after `H_b` under the grandfathered rules. The amount of such future activity is limited by the pre-`H_a` UTXO inventory, not eliminated. Choosing `H_a` early reduces the preparation opportunity but does not justify unexpectedly restricting previously created legitimate outputs.

### Scope, cumulative capacity, and remaining data channels

This proposal prevents non-public-key arbitrary byte strings from being used as multisig public-key operands in affected spends and limits each executed multisig's unused public-key operands to at most `m - ceil(m / 3)`. The additional ratio limits **per-operation** unused slots, not their cumulative total within an input, transaction, or block. Attackers can execute multiple qualifying multisig operations, use multiple inputs, or use multiple transactions; existing script operation, signature-operation, stack, and weight limits still apply, but no new aggregate data budget is introduced.

It does not prevent data encoded into actual public keys, signatures, hashes, ordinary script pushes, dead branches, witness-stack arguments, or other consensus-valid structures. Using compressed-curve-point search, an attacker may preserve substantial runs of original file bytes while modifying a few bytes to form valid points. That encoding may be inexpensive; the minimum threshold does not make such reconstruction cryptographically infeasible. Whether this qualifies as "large, nearly contiguous" storage for purposes of a broader consensus policy requires a separate quantitative threat-model decision. In particular, this proposal is **not** a general transaction data-budget rule and does not by itself guarantee that a block contains no large file payloads.

### Threshold compatibility and open effectiveness questions

The minimum-threshold requirement removes legitimate low-threshold signing arrangements even when every key is mathematically valid. This is independent of the malformed-key issue addressed by the rest of the proposal. Prior to choosing deployment heights, the deploying chain SHOULD audit confirmed script reveals and representative wallet/contract implementations, including 1-of-4, 2-of-7, 3-of-10, 3-of-15, and dynamic stack-generated `n`/`m`. Such a historical scan cannot fully reveal never-spent script-hash UTXOs, so it is not a complete measure of compatibility risk.

The following security and policy questions remain open and do not relax the normative threshold stated in this draft: (1) how much additional suppression the one-third rule delivers beyond full curve validation; (2) whether repeated public keys make the expected signing-cost increase negligible; (3) how much data can be embedded across many permitted multisig invocations and inputs under existing resource limits; and (4) whether cheaply constructed valid curve points containing largely unmodified file substrings fall inside the desired notion of nearly contiguous storage. These questions MUST be resolved or explicitly accepted as limitations before finalizing the deployment.

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
4. Check `1 <= n <= m <= 20` and `3 * n >= m` for every affected executed opcode under the new flag, after existing stack/count validation and before ECDSA matching.
5. Add the unconditional, full-point-validation loop to both affected opcodes' shared interpreter case after threshold validation and before ECDSA matching.
6. Preserve existing legacy/P2SH/P2WSH checks and ECDSA matching semantics.
7. Handle mempool, same-block parent/child spends, reorgs, cached execution results, and block connection consistently.

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
| T04 | 5-of-15 with fifteen valid compressed curve points and five valid signatures | Accept |
| T05 | Valid uncompressed 65-byte keys with prefix `0x04` in legacy/P2SH multisig | Accept |
| T06 | One unused operand has length 32, 64, or 256 bytes | Reject |
| T07 | One unused operand has 33 bytes and `0x02`/`0x03` prefix but invalid curve coordinates | Reject |
| T08 | One unused operand has 65 bytes and `0x04` prefix but invalid curve coordinates | Reject |
| T09 | One unused operand is a valid-looking 65-byte hybrid (`0x06`/`0x07`) encoding | Reject |
| T10 | `0-of-m` executed with **all valid** public-key operands and no ECDSA signatures | Reject: threshold below minimum |
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

| T21 | 3-of-15 with fifteen valid compressed curve points and three valid signatures | Reject: `3 * 3 < 15` |
| T22 | 1-of-3 with three fully valid public keys and one valid signature | Accept: boundary `3 * 1 == 3` |
| T23 | 1-of-4 with four fully valid public keys and one valid signature | Reject: `3 * 1 < 4` |
| T24 | 2-of-6 with six fully valid public keys and two valid signatures | Accept: boundary `3 * 2 == 6` |
| T25 | 2-of-7 with seven fully valid public keys and two valid signatures | Reject: `3 * 2 < 7` |
| T26 | 7-of-20 with twenty fully valid public keys and seven valid signatures | Accept: minimum threshold for `m = 20` |
| T27 | `0-of-0` with no public keys or signatures, when the opcode would previously succeed | Reject: `n >= 1` |
| T28 | 7-of-20 with enough repeated valid public keys and repeated matching valid ECDSA signatures to satisfy the existing matching order | No rejection solely for duplicated keys/signatures |
| T29 | An executed multisig obtains `n` and `m` from stack computations instead of literal `OP_N` instructions, with an otherwise valid but below-one-third ratio | Reject: apply to actual stack operands |
| T30 | Multiple executed multisigs in one eligible input, all valid keys, with only the second operation violating `3 * n >= m` | Reject |
| T31 | Multiple eligible inputs each satisfy the one-third threshold independently, potentially carrying data inside valid curve points | No rejection solely because of aggregate unused slots; evaluate other applicable rules |
| T32 | A 1-of-4 multisig with all valid keys, funded at `H_a - 1` and spent after `H_b` | No rejection from this new rule (grandfathered) |
| T33 | A 1-of-4 multisig with all valid keys, funded at `H_a` and spent at `H_b - 1` | No rejection from this new rule (not yet enforced) |
| T34 | A 1-of-4 multisig with all valid keys, funded at `H_a` and spent at `H_b` | Reject: threshold below minimum |

Vectors MUST additionally exercise boundaries `H_a - 1`, `H_a`, `H_b - 1`, and `H_b`; both opcodes; `m` values 0, 1, 3, 4, 6, 7, 15, and 20; `n = 0`; ratios exactly on and immediately below the `3 * n = m` boundary; mixed-input flag handling; repeated keys/signatures; script caches; and an attempted policy bypass via direct block submission. A test passed by `testmempoolaccept` alone is not evidence of block-level consensus validity.

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
4. Complete a UTXO/script compatibility audit for full curve validation **and the independent one-third threshold requirement**, especially if `H_a` is set in the past; pay particular attention to legitimate low-threshold and hidden script-hash spending conditions.
5. Benchmark adversarial curve parsing, threshold-boundary cases, repeated keys/signatures, and validate script-cache/reorganization behavior.
6. Quantify the incremental effectiveness of `3 * n >= m` against large, nearly contiguous data storage compared with curve validation alone, including multi-operation and multi-input aggregation; record any accepted limitation concerning low-cost curve-point encoding.
