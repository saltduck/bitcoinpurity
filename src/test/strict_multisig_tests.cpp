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

CScript SignBare(const CScript& script_pubkey, const std::vector<CKey>& keys)
{
    const CMutableTransaction credit{BuildCreditingTransaction(script_pubkey)};
    const CMutableTransaction spend{BuildSpendingTransaction(CScript{}, CScriptWitness{}, CTransaction{credit})};
    const uint256 hash{SignatureHash(script_pubkey, spend, 0, SIGHASH_ALL, 0, SigVersion::BASE)};
    CScript script_sig;
    script_sig << OP_0;
    for (const auto& key : keys) {
        valtype sig;
        BOOST_REQUIRE(key.Sign(hash, sig));
        sig.push_back(SIGHASH_ALL);
        script_sig << sig;
    }
    return script_sig;
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

    const CScript duplicate{Multisig(0, {Bytes(k1), Bytes(k1)})};
    BOOST_CHECK_EQUAL(Verify(duplicate, CScript{} << OP_0, STRICT_FLAGS), SCRIPT_ERR_OK);

    BOOST_CHECK_EQUAL(Verify(Multisig(0, {}), CScript{} << OP_0, STRICT_FLAGS), SCRIPT_ERR_OK);

    std::vector<valtype> twenty;
    for (int i = 0; i < MAX_PUBKEYS_PER_MULTISIG; ++i) twenty.push_back(Bytes(NewKey(i % 2 == 0)));
    BOOST_CHECK_EQUAL(Verify(Multisig(0, twenty), CScript{} << OP_0, STRICT_FLAGS), SCRIPT_ERR_OK);
    twenty.back() = valtype(33, 0xab);
    CheckStrictOnly(Multisig(0, twenty), CScript{} << OP_0);
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
    CheckStrictOnly(Multisig(0, {garbage}), CScript{} << OP_0);
    CheckStrictOnly(Multisig(1, {garbage}) << OP_NOT, CScript{} << OP_0 << OP_0);
    CheckStrictOnly(Multisig(0, {garbage}, OP_CHECKMULTISIGVERIFY) << OP_1, CScript{} << OP_0);
}

BOOST_AUTO_TEST_CASE(only_executed_opcodes_are_checked)
{
    const valtype garbage(100, 0xab);
    const valtype key{Bytes(NewKey())};

    CScript skipped{Concat(CScript{} << OP_0 << OP_IF, Multisig(0, {garbage}))};
    skipped << OP_ENDIF << OP_1;
    BOOST_CHECK_EQUAL(Verify(skipped, CScript{}, STRICT_FLAGS), SCRIPT_ERR_OK);

    const CScript both_valid{Concat(Multisig(0, {key}, OP_CHECKMULTISIGVERIFY), Multisig(0, {key}))};
    BOOST_CHECK_EQUAL(Verify(both_valid, CScript{} << OP_0 << OP_0, STRICT_FLAGS), SCRIPT_ERR_OK);

    const CScript second_invalid{Concat(Multisig(0, {key}, OP_CHECKMULTISIGVERIFY), Multisig(0, {garbage}))};
    CheckStrictOnly(second_invalid, CScript{} << OP_0 << OP_0);
}

BOOST_AUTO_TEST_CASE(p2sh_and_p2wsh)
{
    const valtype garbage(100, 0xab);
    const CScript redeem{Multisig(0, {garbage})};
    CheckStrictOnly(GetScriptForDestination(ScriptHash(redeem)), CScript{} << OP_0 << valtype(redeem.begin(), redeem.end()));

    constexpr unsigned int witness_flags{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS};
    const auto witness_spend{[](const CScript& witness_script) {
        CScriptWitness witness;
        witness.stack = {{}, valtype(witness_script.begin(), witness_script.end())};
        return std::pair{GetScriptForDestination(WitnessV0ScriptHash(witness_script)), witness};
    }};

    const auto [uncompressed_spk, uncompressed_witness]{witness_spend(Multisig(0, {Bytes(NewKey(/*compressed=*/false))}))};
    BOOST_CHECK_EQUAL(Verify(uncompressed_spk, CScript{}, witness_flags | SCRIPT_VERIFY_STRICT_MULTISIG_PUBKEYS, uncompressed_witness), SCRIPT_ERR_OK);

    const auto [garbage_spk, garbage_witness]{witness_spend(Multisig(0, {garbage}))};
    CheckStrictOnly(garbage_spk, CScript{}, garbage_witness, witness_flags);
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

    CScript CoinbaseScript() const { return CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG; }
    CScript Invalid() const { return Multisig(1, {ToByteVector(coinbaseKey.GetPubKey()), valtype(100, 0xab)}); }
    CScript Valid() const { return Multisig(1, {ToByteVector(coinbaseKey.GetPubKey()), Bytes(other_key)}); }

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

    const auto before_utxo_height{FundFromCoinbase(0, {Invalid(), Invalid(), Valid()})};
    BOOST_REQUIRE_EQUAL(Connect({before_utxo_height}), UTXO_HEIGHT - 1);
    const auto at_utxo_height{FundFromCoinbase(1, {Invalid(), Invalid(), Invalid(), Valid(), Valid()})};
    BOOST_REQUIRE_EQUAL(Connect({at_utxo_height}), UTXO_HEIGHT);
    BOOST_REQUIRE_EQUAL(Connect({}), UTXO_HEIGHT + 1);

    // The old rules still apply below the enforcement height.
    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 0)}, {payout})}), ENFORCEMENT_HEIGHT - 1);

    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 1)}, {payout})}), ENFORCEMENT_HEIGHT - 1);
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 0)}, {payout})}), ENFORCEMENT_HEIGHT);

    // Grandfathering is per input.
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 1), Output(at_utxo_height, 3)}, {payout})}), ENFORCEMENT_HEIGHT + 1);
    BOOST_CHECK_EQUAL(Connect({Spend({Output(before_utxo_height, 2), Output(at_utxo_height, 2)}, {payout})}), ENFORCEMENT_HEIGHT + 1);

    BOOST_CHECK_EQUAL(Connect({Spend({Output(at_utxo_height, 4)}, {payout})}), ENFORCEMENT_HEIGHT + 2);

    // An output created and spent in the same block uses that block's height.
    const auto parent{FundFromCoinbase(2, {Invalid()})};
    const auto child{Spend({Output(parent, 0)}, {payout})};
    BOOST_CHECK_EQUAL(Connect({parent, child}), ENFORCEMENT_HEIGHT + 2);
    BOOST_CHECK_EQUAL(Connect({parent}), ENFORCEMENT_HEIGHT + 3);
}
