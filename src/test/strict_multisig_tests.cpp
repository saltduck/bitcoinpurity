// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <chainparams.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <key.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <test/util/setup_common.h>
#include <test/util/transaction_utils.h>
#include <util/chaintype.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using valtype = std::vector<unsigned char>;

constexpr unsigned int LEGACY_FLAGS{SCRIPT_VERIFY_P2SH};
constexpr unsigned int STRICT_FLAGS{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICT_MULTISIG_PUBKEYS};

CScript Multisig(int required, const std::vector<valtype>& keys, opcodetype op = OP_CHECKMULTISIG)
{
    CScript script;
    script << required;
    for (const auto& key : keys) script << key;
    script << int64_t(keys.size()) << op;
    return script;
}

CScript Concat(CScript first, const CScript& second)
{
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

ScriptError Verify(const CScript& script_pubkey, const CScript& script_sig, unsigned int flags, const CScriptWitness& witness = {})
{
    const CMutableTransaction credit{BuildCreditingTransaction(script_pubkey)};
    const CMutableTransaction spend{BuildSpendingTransaction(script_sig, witness, CTransaction{credit})};
    ScriptError err{SCRIPT_ERR_UNKNOWN_ERROR};
    VerifyScript(script_sig, script_pubkey, &witness, flags,
                 MutableTransactionSignatureChecker{&spend, 0, credit.vout[0].nValue, MissingDataBehavior::ASSERT_FAIL}, &err);
    return err;
}

std::vector<valtype> SignKeys(const CScript& script_code, const CScript& tx_script_pubkey, const std::vector<CKey>& keys, SigVersion sigversion)
{
    const CMutableTransaction credit{BuildCreditingTransaction(tx_script_pubkey)};
    const CMutableTransaction spend{BuildSpendingTransaction(CScript{}, CScriptWitness{}, CTransaction{credit})};
    std::vector<valtype> sigs;
    for (const auto& key : keys) {
        const uint256 hash{SignatureHash(script_code, spend, 0, SIGHASH_ALL, credit.vout[0].nValue, sigversion)};
        valtype sig;
        BOOST_REQUIRE(key.Sign(hash, sig));
        sig.push_back(SIGHASH_ALL);
        sigs.push_back(std::move(sig));
    }
    return sigs;
}

CScript SignBare(const CScript& script_pubkey, const std::vector<CKey>& keys)
{
    CScript script_sig;
    script_sig << OP_0;
    for (const auto& sig : SignKeys(script_pubkey, script_pubkey, keys, SigVersion::BASE)) script_sig << sig;
    return script_sig;
}

CScript P2SHSig(const CScript& redeem, const std::vector<CKey>& keys)
{
    const CScript program{GetScriptForDestination(ScriptHash(redeem))};
    CScript script_sig;
    script_sig << OP_0;
    for (const auto& sig : SignKeys(redeem, program, keys, SigVersion::BASE)) script_sig << sig;
    script_sig << valtype(redeem.begin(), redeem.end());
    return script_sig;
}

// One key per executed multisig, in execution order. Each opcode consumes the topmost signature.
CScript MultisigSigs(const CScript& script, const std::vector<CKey>& keys_in_execution_order)
{
    const std::vector<valtype> sigs{SignKeys(script, script, keys_in_execution_order, SigVersion::BASE)};
    CScript script_sig;
    for (size_t i = sigs.size(); i-- > 0;) script_sig << OP_0 << sigs[i];
    return script_sig;
}

CScriptWitness P2WSHWitness(const CScript& witness_script, const std::vector<CKey>& keys)
{
    const CScript program{GetScriptForDestination(WitnessV0ScriptHash(witness_script))};
    CScriptWitness witness;
    witness.stack.push_back({});
    for (const auto& sig : SignKeys(witness_script, program, keys, SigVersion::WITNESS_V0)) witness.stack.push_back(sig);
    witness.stack.emplace_back(witness_script.begin(), witness_script.end());
    return witness;
}

CKey NewKey(bool compressed = true)
{
    CKey key;
    key.MakeNewKey(compressed);
    return key;
}

valtype Bytes(const CKey& key) { return ToByteVector(key.GetPubKey()); }

void CheckStrictOnly(const CScript& script_pubkey, const CScript& script_sig, const CScriptWitness& witness = {}, unsigned int base_flags = LEGACY_FLAGS)
{
    BOOST_CHECK_EQUAL(Verify(script_pubkey, script_sig, base_flags, witness), SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(Verify(script_pubkey, script_sig, base_flags | SCRIPT_VERIFY_STRICT_MULTISIG_PUBKEYS, witness), SCRIPT_ERR_MULTISIG_PUBKEY);
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(strict_multisig_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(valid_keys_keep_multisig_semantics)
{
    const CKey k1{NewKey()}, k2{NewKey()}, k3{NewKey()};
    const CScript three{Multisig(2, {Bytes(k1), Bytes(k2), Bytes(k3)})};
    BOOST_CHECK_EQUAL(Verify(three, SignBare(three, {k1, k2}), STRICT_FLAGS), SCRIPT_ERR_OK);

    const CKey uncompressed{NewKey(/*compressed=*/false)};
    const CScript legacy{Multisig(1, {Bytes(uncompressed), Bytes(k1)})};
    BOOST_CHECK_EQUAL(Verify(legacy, SignBare(legacy, {uncompressed}), STRICT_FLAGS), SCRIPT_ERR_OK);

    const CScript one{Multisig(1, {Bytes(k1)})};
    BOOST_CHECK_EQUAL(Verify(one, SignBare(one, {k1}), STRICT_FLAGS), SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(minimum_threshold)
{
    const auto keys_of{[](int count) {
        std::vector<CKey> keys;
        std::vector<valtype> encoded;
        keys.reserve(count);
        for (int i = 0; i < count; ++i) {
            keys.push_back(NewKey());
            encoded.push_back(Bytes(keys.back()));
        }
        return std::pair{std::move(keys), std::move(encoded)};
    }};
    const auto expect_ratio{[&](int required, int count, bool allowed) {
        const auto [keys, encoded]{keys_of(count)};
        std::vector<CKey> signers(keys.begin(), keys.begin() + required);
        const CScript script{Multisig(required, encoded)};
        const ScriptError err{Verify(script, required == 0 ? CScript{} << OP_0 : SignBare(script, signers), STRICT_FLAGS)};
        BOOST_CHECK_EQUAL(err, allowed ? SCRIPT_ERR_OK : SCRIPT_ERR_MULTISIG_THRESHOLD);
        if (!allowed) BOOST_CHECK_EQUAL(Verify(script, required == 0 ? CScript{} << OP_0 : SignBare(script, signers), LEGACY_FLAGS), SCRIPT_ERR_OK);
    }};

    expect_ratio(1, 3, true);   // T22
    expect_ratio(1, 4, false);  // T23
    expect_ratio(2, 6, true);   // T24
    expect_ratio(2, 7, false);  // T25
    expect_ratio(5, 15, true);  // T04
    expect_ratio(3, 15, false); // T21
    expect_ratio(7, 20, true);  // T26
    expect_ratio(0, 0, false);  // T27
    expect_ratio(0, 3, false);  // T10

    const CKey repeated{NewKey()};
    const valtype repeated_bytes{Bytes(repeated)};
    const CScript duplicated{Multisig(7, std::vector<valtype>(20, repeated_bytes))};
    std::vector<CKey> seven(7, repeated);
    BOOST_CHECK_EQUAL(Verify(duplicated, SignBare(duplicated, seven), STRICT_FLAGS), SCRIPT_ERR_OK); // T28

    CScript computed;
    computed << OP_2 << OP_1 << OP_SUB;
    const auto [computed_keys, computed_bytes]{keys_of(4)};
    for (const auto& encoded : computed_bytes) computed << encoded;
    computed << OP_2 << OP_2 << OP_ADD << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(Verify(computed, SignBare(computed, {computed_keys[0]}), LEGACY_FLAGS), SCRIPT_ERR_OK);
    BOOST_CHECK_EQUAL(Verify(computed, SignBare(computed, {computed_keys[0]}), STRICT_FLAGS), SCRIPT_ERR_MULTISIG_THRESHOLD); // T29

    const CKey ok{NewKey()};
    const CScript second_low{Concat(Multisig(1, {Bytes(ok)}, OP_CHECKMULTISIGVERIFY), Multisig(1, {Bytes(ok), Bytes(ok), Bytes(ok), Bytes(ok)}))};
    BOOST_CHECK_EQUAL(Verify(second_low, MultisigSigs(second_low, {ok, ok}), STRICT_FLAGS), SCRIPT_ERR_MULTISIG_THRESHOLD); // T30
}

BOOST_AUTO_TEST_CASE(unused_operands_must_be_valid_points)
{
    const CKey k1{NewKey()}, k2{NewKey()};
    for (const size_t size : {32, 64, 256}) {
        const CScript script{Multisig(2, {Bytes(k1), Bytes(k2), valtype(size, 0xab)})};
        CheckStrictOnly(script, SignBare(script, {k1, k2}));
    }

    valtype off_curve(CPubKey::COMPRESSED_SIZE, 0);
    off_curve[0] = 0x02;
    BOOST_REQUIRE(!CPubKey{off_curve}.IsFullyValid());

    valtype bad_y{Bytes(NewKey(/*compressed=*/false))};
    bad_y.back() ^= 1;
    BOOST_REQUIRE(!CPubKey{bad_y}.IsFullyValid());

    valtype hybrid{Bytes(NewKey(/*compressed=*/false))};
    hybrid[0] = 0x06 | (hybrid.back() & 1);
    BOOST_REQUIRE(CPubKey{hybrid}.IsFullyValid());

    for (const valtype& invalid : {off_curve, bad_y, hybrid}) {
        const CScript script{Multisig(1, {Bytes(k1), invalid})};
        CheckStrictOnly(script, SignBare(script, {k1}));
    }
}

BOOST_AUTO_TEST_CASE(check_does_not_depend_on_result)
{
    const valtype garbage(100, 0xab);
    const CKey key{NewKey()};
    const CScript inverted{Concat(Multisig(1, {Bytes(key), garbage}), CScript{} << OP_NOT)};
    CheckStrictOnly(inverted, CScript{} << OP_0 << OP_0);
    const CScript verify_bad{Concat(Multisig(1, {Bytes(key), garbage}, OP_CHECKMULTISIGVERIFY), CScript{} << OP_1)};
    CheckStrictOnly(verify_bad, SignBare(verify_bad, {key}));
    BOOST_CHECK_EQUAL(Verify(Multisig(0, {Bytes(key)}), CScript{} << OP_0, STRICT_FLAGS), SCRIPT_ERR_MULTISIG_THRESHOLD);
}

BOOST_AUTO_TEST_CASE(only_executed_opcodes_are_checked)
{
    const valtype garbage(100, 0xab);
    const CKey key{NewKey()};

    CScript skipped{Concat(CScript{} << OP_0 << OP_IF, Multisig(1, {garbage, garbage, garbage, garbage}))};
    skipped << OP_ENDIF << OP_1;
    BOOST_CHECK_EQUAL(Verify(skipped, CScript{}, STRICT_FLAGS), SCRIPT_ERR_OK);

    const CScript both_valid{Concat(Multisig(1, {Bytes(key)}, OP_CHECKMULTISIGVERIFY), Multisig(1, {Bytes(key)}))};
    BOOST_CHECK_EQUAL(Verify(both_valid, MultisigSigs(both_valid, {key, key}), STRICT_FLAGS), SCRIPT_ERR_OK);

    const CScript second_invalid{Concat(Multisig(1, {Bytes(key)}, OP_CHECKMULTISIGVERIFY), Multisig(1, {Bytes(key), garbage}))};
    CheckStrictOnly(second_invalid, MultisigSigs(second_invalid, {key, key}));
}

BOOST_AUTO_TEST_CASE(p2sh_and_p2wsh)
{
    const valtype garbage(100, 0xab);
    const CKey key{NewKey()};
    const CScript redeem{Multisig(1, {Bytes(key), garbage})};
    CheckStrictOnly(GetScriptForDestination(ScriptHash(redeem)), P2SHSig(redeem, {key}));

    constexpr unsigned int witness_flags{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS};
    const CKey uncompressed{NewKey(/*compressed=*/false)};
    const CScript uncompressed_script{Multisig(1, {Bytes(uncompressed)})};
    const CScript uncompressed_program{GetScriptForDestination(WitnessV0ScriptHash(uncompressed_script))};
    BOOST_CHECK_EQUAL(Verify(uncompressed_program, CScript{}, witness_flags | SCRIPT_VERIFY_STRICT_MULTISIG_PUBKEYS, P2WSHWitness(uncompressed_script, {uncompressed})), SCRIPT_ERR_OK);

    const CScript garbage_script{Multisig(1, {Bytes(key), garbage})};
    const CScript garbage_program{GetScriptForDestination(WitnessV0ScriptHash(garbage_script))};
    CheckStrictOnly(garbage_program, CScript{}, P2WSHWitness(garbage_script, {key}), witness_flags);
}

BOOST_AUTO_TEST_CASE(regtest_heights_must_be_paired_and_ordered)
{
    const auto make_params{[](std::vector<const char*> argv) {
        ArgsManager args;
        args.AddArg("-testactivationheight=name@height.", "", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
        argv.insert(argv.begin(), "test");
        std::string error;
        BOOST_REQUIRE(args.ParseParameters(argv.size(), argv.data(), error));
        return CreateChainParams(args, ChainType::REGTEST);
    }};

    BOOST_CHECK_THROW(make_params({"-testactivationheight=strictmultisig@105"}), std::runtime_error);
    BOOST_CHECK_THROW(make_params({"-testactivationheight=strictmultisigutxo@102"}), std::runtime_error);
    BOOST_CHECK_THROW(make_params({"-testactivationheight=strictmultisigutxo@105", "-testactivationheight=strictmultisig@105"}), std::runtime_error);

    const auto params{make_params({"-testactivationheight=strictmultisigutxo@102", "-testactivationheight=strictmultisig@105"})};
    BOOST_CHECK_EQUAL(params->GetConsensus().nStrictMultisigUtxoHeight, 102);
    BOOST_CHECK_EQUAL(params->GetConsensus().nStrictMultisigEnforcementHeight, 105);

    const auto main_params{CreateChainParams(ArgsManager{}, ChainType::MAIN)};
    BOOST_CHECK_EQUAL(main_params->GetConsensus().nStrictMultisigEnforcementHeight, std::numeric_limits<int>::max());
}

BOOST_AUTO_TEST_SUITE_END()

namespace {
constexpr int UTXO_HEIGHT{102};
constexpr int ENFORCEMENT_HEIGHT{105};

struct StrictMultisigChainSetup : public TestChain100Setup {
    StrictMultisigChainSetup()
        : TestChain100Setup{ChainType::REGTEST, {.extra_args = {
              "-testactivationheight=strictmultisigutxo@102",
              "-testactivationheight=strictmultisig@105",
          }}} {}

    const CKey other_key{NewKey()};
    const CKey extra_a{NewKey()};
    const CKey extra_b{NewKey()};

    CScript CoinbaseScript() const { return CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG; }
    CScript Invalid() const { return Multisig(1, {ToByteVector(coinbaseKey.GetPubKey()), valtype(100, 0xab)}); }
    CScript Valid() const { return Multisig(1, {ToByteVector(coinbaseKey.GetPubKey()), Bytes(other_key)}); }
    CScript LowRatio() const { return Multisig(1, {ToByteVector(coinbaseKey.GetPubKey()), Bytes(other_key), Bytes(extra_a), Bytes(extra_b)}); }

    CMutableTransaction Spend(const std::vector<std::pair<COutPoint, CScript>>& inputs, const std::vector<CScript>& outputs, bool multisig_inputs = true)
    {
        CMutableTransaction tx;
        for (const auto& [outpoint, script] : inputs) tx.vin.emplace_back(outpoint);
        for (const auto& script : outputs) tx.vout.emplace_back(COIN, script);
        for (size_t i = 0; i < inputs.size(); ++i) {
            const uint256 hash{SignatureHash(inputs[i].second, tx, i, SIGHASH_ALL, 0, SigVersion::BASE)};
            valtype sig;
            BOOST_REQUIRE(coinbaseKey.Sign(hash, sig));
            sig.push_back(SIGHASH_ALL);
            tx.vin[i].scriptSig = multisig_inputs ? CScript() << OP_0 << sig : CScript() << sig;
        }
        return tx;
    }

    CMutableTransaction FundFromCoinbase(size_t coinbase, const std::vector<CScript>& outputs)
    {
        return Spend({{COutPoint{m_coinbase_txns[coinbase]->GetHash(), 0}, CoinbaseScript()}}, outputs, /*multisig_inputs=*/false);
    }

    int Connect(const std::vector<CMutableTransaction>& txs)
    {
        CreateAndProcessBlock(txs, CoinbaseScript());
        return WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height());
    }
};

std::pair<COutPoint, CScript> Output(const CMutableTransaction& tx, uint32_t n)
{
    return {COutPoint{tx.GetHash(), n}, tx.vout[n].scriptPubKey};
}
} // namespace

BOOST_FIXTURE_TEST_CASE(strict_multisig_block_heights, StrictMultisigChainSetup)
{
    const CScript payout{CoinbaseScript()};

    const auto before_utxo_height{FundFromCoinbase(0, {Invalid(), Invalid(), Valid(), LowRatio()})};
    BOOST_REQUIRE_EQUAL(Connect({before_utxo_height}), UTXO_HEIGHT - 1);
    const auto at_utxo_height{FundFromCoinbase(1, {Invalid(), Invalid(), Invalid(), Valid(), Valid(), LowRatio(), LowRatio()})};
    BOOST_REQUIRE_EQUAL(Connect({at_utxo_height}), UTXO_HEIGHT);
    BOOST_REQUIRE_EQUAL(Connect({}), UTXO_HEIGHT + 1);

    // The old rules still apply below the enforcement height, for both invalid keys and a 1-of-4 threshold.
    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 0)}, {payout}), Spend({Output(at_utxo_height, 5)}, {payout})}), ENFORCEMENT_HEIGHT - 1);

    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 1)}, {payout})}), ENFORCEMENT_HEIGHT - 1);
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 0)}, {payout})}), ENFORCEMENT_HEIGHT);

    // Grandfathering is per input.
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 1), Output(at_utxo_height, 3)}, {payout})}), ENFORCEMENT_HEIGHT + 1);
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 2), Output(at_utxo_height, 2)}, {payout})}), ENFORCEMENT_HEIGHT + 1);

    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 4)}, {payout})}), ENFORCEMENT_HEIGHT + 2);

    // A 1-of-4 with valid keys is grandfathered only when created below H_a.
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 3)}, {payout})}), ENFORCEMENT_HEIGHT + 3);
    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 6)}, {payout})}), ENFORCEMENT_HEIGHT + 3);

    // An output created and spent in the same block uses that block's height.
    const auto parent{FundFromCoinbase(2, {Invalid()})};
    const auto child{Spend({Output(parent, 0)}, {payout})};
    BOOST_CHECK_EQUAL(Connect({parent, child}), ENFORCEMENT_HEIGHT + 2);
    BOOST_CHECK_EQUAL(Connect({parent}), ENFORCEMENT_HEIGHT + 3);
}
