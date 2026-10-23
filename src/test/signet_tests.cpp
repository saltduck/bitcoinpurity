// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <hash.h>
#include <key.h>
#include <node/context.h>
#include <pow.h>
#include <script/interpreter.h>
#include <signet.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <test/data/bitcoin_signet_block11.json.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <validation.h>

#include <array>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {
struct SignetBlockFactory {
    Consensus::Params params{CChainParams::SigNet({})->GetConsensus()};
    std::array<CKey, 3> old_keys;
    std::array<CKey, 3> new_keys;

    SignetBlockFactory()
    {
        params.nPurityActivationHeight = 11;
        for (int i = 0; i < 3; ++i) {
            std::array<unsigned char, 32> secret{};
            secret.back() = 9 + i;
            old_keys[i].Set(secret.begin(), secret.end(), true);
            secret.back() = 42 + i;
            new_keys[i].Set(secret.begin(), secret.end(), true);
        }
        for (auto [keys, output] : {std::pair{&old_keys, &params.signet_challenge}, std::pair{&new_keys, &params.signet_challenge_postfork}}) {
            CScript challenge;
            challenge << OP_2;
            for (const auto& key : *keys) challenge << ToByteVector(key.GetPubKey());
            challenge << OP_3 << OP_CHECKMULTISIG;
            output->assign(challenge.begin(), challenge.end());
        }
    }

    CBlock UnsignedBlock()
    {
        CBlock block;
        block.nVersion = 0x20000000;
        block.nTime = 1700000000;
        block.nBits = 0x1e0377ae;
        CMutableTransaction cb;
        cb.vin.resize(1);
        cb.vin[0].scriptSig = CScript() << 11;
        cb.vin[0].scriptWitness.stack.emplace_back(32, 0);
        cb.vout.emplace_back(50 * COIN, CScript() << OP_TRUE);
        std::vector<unsigned char> commitment{0xaa, 0x21, 0xa9, 0xed};
        const uint256 witness_hash = Hash(std::vector<unsigned char>(64, 0));
        commitment.insert(commitment.end(), witness_hash.begin(), witness_hash.end());
        cb.vout.emplace_back(0, CScript() << OP_RETURN << commitment);
        block.vtx.push_back(MakeTransactionRef(cb));
        block.hashMerkleRoot = BlockMerkleRoot(block);
        return block;
    }

    CBlock Sign(CBlock block, bool historical = false, std::array<int, 2> signers = {0, 1}, bool extra_item = false, bool trailing = false, bool maximum_signature = false)
    {
        CMutableTransaction cb{*block.vtx[0]};
        const int index = GetWitnessCommitmentIndex(block);
        cb.vout[index].scriptPubKey.resize(38);
        cb.vout[index].scriptPubKey << std::vector<unsigned char>{0xec, 0xc7, 0xda, 0xa2};
        if (trailing) cb.vout[index].scriptPubKey << OP_1;
        block.vtx[0] = MakeTransactionRef(cb);
        const auto& bytes = historical ? params.signet_challenge : params.signet_challenge_postfork;
        const CScript challenge{bytes.begin(), bytes.end()};
        const auto txs = SignetTxs::Create(block, challenge);
        BOOST_REQUIRE(txs);
        const auto digest = SignatureHash(challenge, txs->m_to_sign, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        CScript script_sig;
        if (extra_item) script_sig << OP_1;
        script_sig << OP_0;
        for (int signer : signers) {
            std::vector<unsigned char> signature;
            BOOST_REQUIRE((historical ? old_keys : new_keys)[signer].Sign(digest, signature));
            if (maximum_signature) {
                uint32_t entropy{0};
                do {
                    BOOST_REQUIRE(new_keys[signer].Sign(digest, signature, false, ++entropy));
                } while (signature[3] != 33 || signature[38] != 32);
                uint256 low_s;
                std::reverse_copy(signature.begin() + 39, signature.end(), low_s.begin());
                const uint256 order{"fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"};
                const auto high_s = ArithToUint256(UintToArith256(order) - UintToArith256(low_s));
                signature.resize(38);
                signature.push_back(33);
                signature.push_back(0);
                for (auto it = high_s.end(); it != high_s.begin();) signature.push_back(*--it);
                signature[1] = signature.size() - 2;
            }
            signature.push_back(SIGHASH_ALL);
            script_sig << signature;
        }
        std::vector<unsigned char> solution{0xec, 0xc7, 0xda, 0xa2};
        VectorWriter writer{solution, solution.size()};
        writer << script_sig << std::vector<std::vector<unsigned char>>{};
        cb.vout[index].scriptPubKey.resize(38);
        cb.vout[index].scriptPubKey << solution;
        if (trailing) cb.vout[index].scriptPubKey << OP_1;
        block.vtx[0] = MakeTransactionRef(cb);
        block.hashMerkleRoot = BlockMerkleRoot(block);
        return block;
    }
};
struct SignetTestingSetup : BasicTestingSetup, SignetBlockFactory {};
}

BOOST_FIXTURE_TEST_SUITE(signet_tests, SignetTestingSetup)

BOOST_AUTO_TEST_CASE(challenge_switch_uses_explicit_height)
{
    const auto old_block = Sign(UnsignedBlock(), true);
    const auto new_block = Sign(UnsignedBlock());
    BOOST_CHECK(CheckSignetBlockSolution(old_block, params, 10));
    BOOST_CHECK(!CheckSignetBlockSolution(old_block, params, 11));
    BOOST_CHECK(!CheckSignetBlockSolution(new_block, params, 10));
    BOOST_CHECK(CheckSignetBlockSolution(new_block, params, 11));
    BOOST_CHECK(CheckSignetBlockSolution(new_block, params, 12));
    BOOST_CHECK(!CheckSignetBlockSolution(UnsignedBlock(), params, 11));
    for (const auto& pair : {std::array{0, 1}, std::array{0, 2}, std::array{1, 2}}) {
        BOOST_CHECK(CheckSignetBlockSolution(Sign(UnsignedBlock(), false, pair), params, 11));
    }
}

BOOST_AUTO_TEST_CASE(real_original_signet_fork_block_loses_authority)
{
    UniValue fixture;
    BOOST_REQUIRE(fixture.read(json_tests::bitcoin_signet_block11));
    DataStream stream{ParseHex(fixture["raw"].get_str())};
    CBlock block;
    stream >> TX_WITH_WITNESS(block);
    BOOST_REQUIRE_EQUAL(block.GetHash().GetHex(), fixture["hash"].get_str());
    auto historical = CChainParams::SigNet({})->GetConsensus();
    BOOST_REQUIRE(CheckProofOfWork(block.GetHash(), block.nBits, historical));
    BOOST_REQUIRE(CheckSignetBlockSolution(block, historical, 11));
    historical.nPurityActivationHeight = 11;
    historical.signet_challenge_postfork = params.signet_challenge_postfork;
    BOOST_REQUIRE(CheckSignetBlockSolution(block, historical, 10));
    block.fChecked = true;
    BOOST_CHECK(!CheckSignetBlockSolution(block, historical, 11));
}

BOOST_AUTO_TEST_CASE(commitment_exception_is_signet_only)
{
    const auto block = Sign(UnsignedBlock());
    const auto size = block.vtx[0]->vout[1].scriptPubKey.size();
    BOOST_REQUIRE(size > 83);
    BOOST_REQUIRE(size <= 195);
    TxValidationState old_state;
    BOOST_CHECK(!Consensus::CheckOutputSizes(*block.vtx[0], old_state));
    BOOST_CHECK_EQUAL(old_state.GetRejectReason(), "bad-txns-vout-script-toolarge");
    TxValidationState state;
    BOOST_CHECK(CheckSignetCoinbaseOutputSizes(block, params, 11, state));
    const auto main = CChainParams::Main({})->GetConsensus();
    TxValidationState main_state;
    BOOST_CHECK(!CheckSignetCoinbaseOutputSizes(block, main, 961637, main_state));
    BOOST_CHECK_EQUAL(main_state.GetRejectReason(), old_state.GetRejectReason());
}

BOOST_AUTO_TEST_CASE(extra_stack_and_payload_are_not_authorized)
{
    const auto old_extra = Sign(UnsignedBlock(), true, {0, 1}, true, true);
    BOOST_CHECK(CheckSignetBlockSolution(old_extra, params, 10));
    BOOST_CHECK(!CheckSignetBlockSolution(Sign(UnsignedBlock(), false, {0, 1}, true), params, 11));
    BOOST_CHECK(!CheckSignetBlockSolution(Sign(UnsignedBlock(), false, {0, 1}, false, true), params, 11));
    auto block = Sign(UnsignedBlock());
    CMutableTransaction cb{*block.vtx[0]};
    cb.vout[1].scriptPubKey << std::vector<unsigned char>(1000, 0);
    block.vtx[0] = MakeTransactionRef(cb);
    BOOST_CHECK(!CheckSignetBlockSolution(block, params, 11));
}

BOOST_AUTO_TEST_CASE(other_coinbase_outputs_keep_rdts_limits)
{
    auto block = UnsignedBlock();
    CMutableTransaction cb{*block.vtx[0]};
    cb.vout.emplace_back(0, CScript() << OP_RETURN << std::vector<unsigned char>(81, 1));
    block.vtx[0] = MakeTransactionRef(cb);
    block = Sign(block);
    BOOST_CHECK(CheckSignetBlockSolution(block, params, 11));
    TxValidationState state;
    BOOST_CHECK(!CheckSignetCoinbaseOutputSizes(block, params, 11, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-vout-script-toolarge");
}

BOOST_AUTO_TEST_CASE(highest_commitment_is_the_only_exception)
{
    auto block = UnsignedBlock();
    CMutableTransaction cb{*block.vtx[0]};
    cb.vout.push_back(cb.vout[1]);
    block.vtx[0] = MakeTransactionRef(cb);
    block = Sign(block);
    BOOST_CHECK_EQUAL(GetWitnessCommitmentIndex(block), 2);
    BOOST_CHECK(CheckSignetBlockSolution(block, params, 11));
    TxValidationState state;
    BOOST_CHECK(CheckSignetCoinbaseOutputSizes(block, params, 11, state));
    cb = CMutableTransaction{*block.vtx[0]};
    cb.vout[1].scriptPubKey << std::vector<unsigned char>(50, 0);
    block.vtx[0] = MakeTransactionRef(cb);
    block = Sign(block);
    BOOST_CHECK(CheckSignetBlockSolution(block, params, 11));
    TxValidationState over;
    BOOST_CHECK(!CheckSignetCoinbaseOutputSizes(block, params, 11, over));
}

BOOST_AUTO_TEST_CASE(signature_binds_block_fields)
{
    auto block = Sign(UnsignedBlock());
    block.nTime++;
    BOOST_CHECK(!CheckSignetBlockSolution(block, params, 11));
    block = Sign(UnsignedBlock());
    block.hashPrevBlock = uint256{"0000000000000000000000000000000000000000000000000000000000000001"};
    BOOST_CHECK(!CheckSignetBlockSolution(block, params, 11));
}

BOOST_AUTO_TEST_CASE(canonical_commitment_accepts_exact_maximum)
{
    auto block = Sign(UnsignedBlock(), false, {0, 1}, false, false, true);
    BOOST_REQUIRE_EQUAL(block.vtx[0]->vout[1].scriptPubKey.size(), 195);
    BOOST_CHECK(CheckSignetBlockSolution(block, params, 11));
    TxValidationState state;
    BOOST_CHECK(CheckSignetCoinbaseOutputSizes(block, params, 11, state));
    CMutableTransaction cb{*block.vtx[0]};
    cb.vout[1].scriptPubKey << OP_0;
    block.vtx[0] = MakeTransactionRef(cb);
    BOOST_CHECK(!CheckSignetBlockSolution(block, params, 11));
}

BOOST_AUTO_TEST_CASE(noncanonical_solution_encodings_are_rejected)
{
    const auto original = Sign(UnsignedBlock());
    const auto& commitment = original.vtx[0]->vout[1].scriptPubKey;
    auto pc = commitment.begin() + 38;
    opcodetype op;
    std::vector<unsigned char> solution;
    BOOST_REQUIRE(commitment.GetOp(pc, op, solution));
    for (int mutation = 0; mutation < 4; ++mutation) {
        auto data = solution;
        CScript suffix;
        if (mutation == 0) {
            data.insert(data.begin() + 5, 0);
            data.insert(data.begin() + 4, 0xfd);
        } else if (mutation == 1) {
            data.back() = 1;
            data.push_back(0);
        } else if (mutation == 2) {
            suffix << OP_PUSHDATA2;
            suffix.push_back(data.size());
            suffix.push_back(0);
            suffix.insert(suffix.end(), data.begin(), data.end());
        } else {
            data.push_back(0);
        }
        if (suffix.empty()) suffix << data;
        auto block = original;
        CMutableTransaction cb{*block.vtx[0]};
        cb.vout[1].scriptPubKey.resize(38);
        cb.vout[1].scriptPubKey.insert(cb.vout[1].scriptPubKey.end(), suffix.begin(), suffix.end());
        block.vtx[0] = MakeTransactionRef(cb);
        BOOST_CHECK(!CheckSignetBlockSolution(block, params, 11));
    }
}

BOOST_FIXTURE_TEST_CASE(cached_signet_block_still_requires_pow, TestChain100Setup)
{
    SignetBlockFactory factory;
    auto& chainman = *m_node.chainman;
    auto& params = const_cast<Consensus::Params&>(chainman.GetConsensus());
    params.signet_blocks = true;
    params.nPurityActivationHeight = 101;
    params.nAsertAnchorHeight = 99;
    params.signet_challenge_postfork = factory.params.signet_challenge_postfork;
    auto block = factory.Sign(CreateBlock({}, CScript() << OP_TRUE, chainman.ActiveChainstate()));
    BOOST_REQUIRE(CheckSignetBlockSolution(block, params, 101));
    while (CheckProofOfWork(block.GetHash(), block.nBits, params)) ++block.nNonce;
    block.fChecked = true;
    LOCK(cs_main);
    auto& chainstate = chainman.ActiveChainstate();
    CBlockIndex index{block};
    const auto hash = block.GetHash();
    index.phashBlock = &hash;
    index.pprev = chainstate.m_chain.Tip();
    index.nHeight = 101;
    index.BuildSkip();
    CCoinsViewCache view{&chainstate.CoinsTip()};
    BlockValidationState state;
    BOOST_CHECK(!chainstate.ConnectBlock(block, state, &index, view));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "high-hash");
    BOOST_CHECK_EQUAL(view.GetBestBlock(), index.pprev->GetBlockHash());
}

BOOST_AUTO_TEST_SUITE_END()
