// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <hash.h>
#include <key_io.h>
#include <rpc/rdtsgrandfather.h>
#include <script/interpreter.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

using namespace rdts_audit;

BOOST_FIXTURE_TEST_SUITE(rdtsgrandfather_tests, BasicTestingSetup)

static constexpr unsigned int NORMAL_FLAGS{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT | REDUCED_DATA_MANDATORY_VERIFY_FLAGS};

BOOST_AUTO_TEST_CASE(candidate_flags_and_mixed_inputs)
{
    constexpr int fixed{961637}, boundary{965664};
    for (int prev : {961636, 961637, 964000, 965663, 965664}) {
        const auto old{OldFlags(NORMAL_FLAGS, prev, boundary)};
        BOOST_CHECK_EQUAL(IsCandidate(prev, fixed, old), prev >= fixed && prev < boundary);
        BOOST_CHECK_EQUAL(old, prev < boundary ? NORMAL_FLAGS & ~REDUCED_DATA_MANDATORY_VERIFY_FLAGS : NORMAL_FLAGS);
        BOOST_CHECK_EQUAL(StrictFlags(old), old | REDUCED_DATA_MANDATORY_VERIFY_FLAGS);
        BOOST_CHECK_EQUAL(old & ~REDUCED_DATA_MANDATORY_VERIFY_FLAGS, NORMAL_FLAGS & ~REDUCED_DATA_MANDATORY_VERIFY_FLAGS);
    }
    for (int old_boundary : {0, fixed - 1, fixed}) {
        for (int prev : {fixed - 1, fixed, fixed + 1}) {
            BOOST_CHECK(!IsCandidate(prev, fixed, OldFlags(NORMAL_FLAGS, prev, old_boundary)));
        }
    }
    const std::vector<int> mixed{960000, 964000, 965664};
    std::vector<size_t> selected;
    for (size_t vin{0}; vin < mixed.size(); ++vin) {
        if (IsCandidate(mixed[vin], fixed, OldFlags(NORMAL_FLAGS, mixed[vin], boundary))) selected.push_back(vin);
    }
    BOOST_REQUIRE_EQUAL(selected.size(), 1);
    BOOST_CHECK_EQUAL(selected.front(), 1);
    BOOST_CHECK(IsCandidate(fixed, fixed, NORMAL_FLAGS & ~SCRIPT_VERIFY_DISCOURAGE_OP_SUCCESS));
}

BOOST_AUTO_TEST_CASE(classification_and_completeness)
{
    BOOST_CHECK(Classify(true, true) == Outcome::OLD_PASS_STRICT_PASS);
    BOOST_CHECK(Classify(true, false) == Outcome::OLD_PASS_STRICT_FAIL);
    BOOST_CHECK(Classify(false, false) == Outcome::OLD_FAIL_STRICT_FAIL);
    BOOST_CHECK(Classify(false, true) == Outcome::OLD_FAIL_STRICT_PASS);
    BOOST_CHECK(IsComplete(0, 0, 0, false));
    BOOST_CHECK(!IsComplete(1, 0, 0, false));
    BOOST_CHECK(!IsComplete(0, 1, 0, false));
    BOOST_CHECK(!IsComplete(0, 0, 1, false));
    BOOST_CHECK(!IsComplete(0, 0, 0, true));
}

static void CheckComparison(const CMutableTransaction& spending, const std::vector<CTxOut>& spent, unsigned int vin, ScriptError expected)
{
    const CTransaction tx{spending};
    auto outputs{spent};
    PrecomputedTransactionData txdata;
    txdata.Init(tx, std::move(outputs));
    SignatureCache cache{0};
    const auto result{CompareInput(spent[vin], tx, cache, vin, NORMAL_FLAGS & ~REDUCED_DATA_MANDATORY_VERIFY_FLAGS, txdata)};
    BOOST_CHECK(!result.old_error.has_value());
    if (expected == SCRIPT_ERR_OK) {
        BOOST_CHECK(!result.strict_error.has_value());
        BOOST_CHECK(result.GetOutcome() == Outcome::OLD_PASS_STRICT_PASS);
    } else {
        BOOST_REQUIRE(result.strict_error.has_value());
        BOOST_CHECK_EQUAL(result.strict_error->first, expected);
        BOOST_CHECK(result.GetOutcome() == Outcome::OLD_PASS_STRICT_FAIL);
    }
}

static void CheckTaproot(const CScript& script, int leaf_version, int depth, const std::vector<std::vector<unsigned char>>& args, bool annex, ScriptError expected)
{
    const std::vector<unsigned char> bytes{script.begin(), script.end()};
    TaprootBuilder builder;
    builder.Add(depth, bytes, leaf_version);
    for (int d{depth}; d > 0; --d) builder.Add(d, {}, 0xc0, false);
    BOOST_REQUIRE(builder.IsComplete());
    builder.Finalize(XOnlyPubKey::NUMS_H);
    const auto data{builder.GetSpendData()};
    const auto control{*data.scripts.at({bytes, leaf_version}).begin()};
    BOOST_CHECK_EQUAL(control.size(), 33 + 32 * depth);
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.emplace_back(1, CScript{} << OP_TRUE);
    tx.vin[0].scriptWitness.stack = args;
    tx.vin[0].scriptWitness.stack.push_back(bytes);
    tx.vin[0].scriptWitness.stack.push_back(control);
    if (annex) tx.vin[0].scriptWitness.stack.push_back({0x50, 0x01});
    CheckComparison(tx, {CTxOut{2, GetScriptForDestination(builder.GetOutput())}}, 0, expected);
}

BOOST_AUTO_TEST_CASE(native_rdts_flag_families)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vout.emplace_back(1, CScript{} << OP_TRUE);
    const std::vector<unsigned char> large(257, 1);
    const CScript pushed{CScript{} << large << OP_DROP << OP_TRUE};
    CheckComparison(tx, {CTxOut{2, pushed}}, 0, SCRIPT_ERR_PUSH_SIZE);

    const CScript redeem{CScript{} << OP_DROP << OP_TRUE};
    tx.vin[0].scriptSig = CScript{} << large << std::vector<unsigned char>(redeem.begin(), redeem.end());
    CheckComparison(tx, {CTxOut{2, GetScriptForDestination(ScriptHash{redeem})}}, 0, SCRIPT_ERR_PUSH_SIZE);
    const CScript large_redeem{CScript{} << std::vector<unsigned char>(256, 1) << OP_DROP << OP_TRUE};
    BOOST_CHECK_GT(large_redeem.size(), 256);
    tx.vin[0].scriptSig = CScript{} << std::vector<unsigned char>(large_redeem.begin(), large_redeem.end());
    CheckComparison(tx, {CTxOut{2, GetScriptForDestination(ScriptHash{large_redeem})}}, 0, SCRIPT_ERR_OK);
    tx.vin[0].scriptSig.clear();
    tx.vin[0].scriptWitness.stack = {large, {redeem.begin(), redeem.end()}};
    CheckComparison(tx, {CTxOut{2, GetScriptForDestination(WitnessV0ScriptHash{redeem})}}, 0, SCRIPT_ERR_PUSH_SIZE);
    tx.vin[0].scriptWitness.stack.clear();
    CheckComparison(tx, {CTxOut{2, CScript{} << OP_2 << std::vector<unsigned char>(32, 1)}}, 0, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM);

    CheckTaproot(CScript{} << OP_TRUE, 0xc0, 0, {}, true, SCRIPT_ERR_PUSH_SIZE);
    CheckTaproot(CScript{} << OP_TRUE, 0xc0, 8, {}, false, SCRIPT_ERR_TAPROOT_WRONG_CONTROL_SIZE);
    CheckTaproot(CScript{} << OP_TRUE, 0xc2, 0, {}, false, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_TAPROOT_VERSION);
    CheckTaproot(CScript{} << OP_DROP << OP_TRUE, 0xc0, 0, {large}, false, SCRIPT_ERR_PUSH_SIZE);
    CheckTaproot(pushed, 0xc0, 0, {}, false, SCRIPT_ERR_PUSH_SIZE);
    for (int opcode{0}; opcode <= 255; ++opcode) {
        if (IsOpSuccess(static_cast<opcodetype>(opcode))) CheckTaproot(CScript{} << static_cast<opcodetype>(opcode), 0xc0, 0, {}, false, SCRIPT_ERR_DISCOURAGE_OP_SUCCESS);
    }
    CheckTaproot(CScript{} << OP_IF << OP_TRUE << OP_ENDIF, 0xc0, 0, {{1}}, false, SCRIPT_ERR_TAPSCRIPT_MINIMALIF);
    CheckTaproot(CScript{} << OP_NOTIF << OP_TRUE << OP_ENDIF, 0xc0, 0, {{}}, false, SCRIPT_ERR_TAPSCRIPT_MINIMALIF);
    CheckTaproot(CScript{} << OP_TRUE, 0xc0, 7, {}, false, SCRIPT_ERR_OK);
    const std::vector<unsigned char> limit(256, 1);
    CheckTaproot(CScript{} << OP_DROP << OP_TRUE, 0xc0, 0, {limit}, false, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(mixed_taproot_sighash_uses_all_spent_outputs)
{
    CKey key;
    key.MakeNewKey(true);
    const uint256 no_scripts{};
    const auto output_key{XOnlyPubKey{key.GetPubKey()}.CreateTapTweak(nullptr)};
    BOOST_REQUIRE(output_key.has_value());
    const std::vector<CTxOut> spent{
        CTxOut{2, CScript{} << std::vector<unsigned char>(257, 1) << OP_DROP << OP_TRUE},
        CTxOut{3, GetScriptForDestination(WitnessV1Taproot{output_key->first})},
    };
    CMutableTransaction tx;
    tx.vin.resize(2);
    tx.vin[1].prevout.n = 1;
    tx.vin[1].scriptWitness.stack = {std::vector<unsigned char>(64)};
    tx.vout.emplace_back(4, CScript{} << OP_TRUE);
    PrecomputedTransactionData txdata;
    auto outputs{spent};
    txdata.Init(tx, std::move(outputs));
    ScriptExecutionData execdata;
    execdata.m_annex_init = true;
    execdata.m_annex_present = false;
    uint256 sighash;
    BOOST_REQUIRE(SignatureHashSchnorr(sighash, execdata, tx, 1, SIGHASH_DEFAULT, SigVersion::TAPROOT, txdata, MissingDataBehavior::FAIL));
    BOOST_REQUIRE(key.SignSchnorr(sighash, tx.vin[1].scriptWitness.stack[0], &no_scripts, uint256{}));
    CheckComparison(tx, spent, 1, SCRIPT_ERR_OK);
    // vin0 remains legitimately grandfathered; checking vin1 does not enforce vin0.
    CheckComparison(tx, spent, 0, SCRIPT_ERR_PUSH_SIZE);
}

BOOST_AUTO_TEST_SUITE_END()
