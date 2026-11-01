// Copyright (c) 2011-2022 The Bitcoin Core developers
// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/merkle.h>
#include <interfaces/mining.h>
#include <node/miner.h>
#include <pow.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validation.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

using interfaces::BlockTemplate;

namespace {

struct TipSnapshot {
    int height{0};
    uint256 hash;
};

TipSnapshot ActiveTip(const node::NodeContext& node)
{
    LOCK(::cs_main);
    const CBlockIndex* tip{Assert(node.chainman)->ActiveChain().Tip()};
    BOOST_REQUIRE(tip);
    return {tip->nHeight, tip->GetBlockHash()};
}

CTransactionRef WithoutCoinbaseWitness(const CTransactionRef& coinbase)
{
    CMutableTransaction mtx{*coinbase};
    mtx.vin.at(0).scriptWitness.SetNull();
    return MakeTransactionRef(std::move(mtx));
}

CTransactionRef WithReservedValue(const CTransactionRef& coinbase, uint8_t byte)
{
    CMutableTransaction mtx{*coinbase};
    mtx.vin.at(0).scriptWitness.stack.assign(1, std::vector<unsigned char>(32, byte));
    return MakeTransactionRef(std::move(mtx));
}

//! Solve the header in place. Regtest's pow limit makes this immediate.
void SolveHeader(CBlockHeader& header, const Consensus::Params& params)
{
    header.nNonce = 0;
    while (!CheckProofOfWork(header.GetHash(), header.nBits, params)) {
        BOOST_REQUIRE_NE(header.nNonce, std::numeric_limits<uint32_t>::max());
        ++header.nNonce;
    }
}

//! Reproduce the block submitSolution() will assemble, including its proof of work.
CBlock SolvedSubmission(BlockTemplate& block_template, const CTransactionRef& coinbase, const Consensus::Params& params)
{
    CBlock block{block_template.getBlock()};
    if (block.vtx.empty()) {
        block.vtx.push_back(coinbase);
    } else {
        block.vtx[0] = coinbase;
    }
    block.hashMerkleRoot = BlockMerkleRoot(block);
    SolveHeader(block, params);
    return block;
}

std::unique_ptr<BlockTemplate> FreshTemplate(node::NodeContext& node)
{
    auto mining{interfaces::MakeMining(node)};
    BOOST_REQUIRE(mining);
    std::unique_ptr<BlockTemplate> block_template{mining->createNewBlock()};
    BOOST_REQUIRE(block_template);
    const CBlock& block{block_template->getBlock()};
    // CreateNewBlock() runs TestBlockValidity(), which must have cached the
    // witness commitment. The regression tests rely on that real cache, not
    // on a flag assigned by the test.
    BOOST_REQUIRE(!block.vtx.empty());
    BOOST_REQUIRE(block.vtx[0]->HasWitness());
    BOOST_REQUIRE(block.vtx[0]->vin[0].scriptWitness.stack.size() == 1);
    BOOST_REQUIRE_EQUAL(block.vtx[0]->vin[0].scriptWitness.stack[0].size(), 32U);
    BOOST_REQUIRE(block.m_checked_witness_commitment);
    BOOST_REQUIRE(!block.m_checked_merkle_root);
    BOOST_REQUIRE(!block.fChecked);
    return block_template;
}

void RequireTipUnchanged(const node::NodeContext& node, const TipSnapshot& before)
{
    const TipSnapshot after{ActiveTip(node)};
    BOOST_CHECK_EQUAL(after.height, before.height);
    BOOST_CHECK(after.hash == before.hash);
}

void RequireIndependentBlockValid(node::NodeContext& node, const uint256& hash, const CTransactionRef& coinbase)
{
    CBlock from_disk;
    {
        LOCK(::cs_main);
        ChainstateManager& chainman{*Assert(node.chainman)};
        const CBlockIndex* index{chainman.m_blockman.LookupBlockIndex(hash)};
        BOOST_REQUIRE(index);
        BOOST_REQUIRE(chainman.ActiveChain().Contains(index));
        BOOST_REQUIRE(chainman.m_blockman.ReadBlock(from_disk, *index));
    }
    // Deserialization does not restore cached validation flags.
    BOOST_CHECK(!from_disk.fChecked);
    BOOST_CHECK(!from_disk.m_checked_merkle_root);
    BOOST_CHECK(!from_disk.m_checked_witness_commitment);
    BlockValidationState state;
    BOOST_CHECK(CheckBlock(from_disk, state, Assert(node.chainman)->GetConsensus()));
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(!IsBlockMutated(from_disk, /*check_witness_root=*/true));
    BOOST_CHECK(from_disk.vtx.at(0)->GetWitnessHash() == coinbase->GetWitnessHash());
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(miner_cached_validation_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(submit_solution_missing_witness_then_valid)
{
    const Consensus::Params& params{Assert(m_node.chainman)->GetConsensus()};
    std::unique_ptr<BlockTemplate> block_template{FreshTemplate(m_node)};
    const CTransactionRef original{block_template->getCoinbaseTx()};
    const uint256 original_witness{original->GetWitnessHash()};
    const TipSnapshot tip_before{ActiveTip(m_node)};

    const CTransactionRef missing_witness{WithoutCoinbaseWitness(original)};
    BOOST_CHECK(!missing_witness->HasWitness());
    const CBlock invalid{SolvedSubmission(*block_template, missing_witness, params)};
    // Processing can fail for more than one reason. The chain tip is the
    // consensus result: a missing witness must not become the active tip.
    BOOST_CHECK(!block_template->submitSolution(invalid.nVersion, invalid.nTime, invalid.nNonce, missing_witness));
    RequireTipUnchanged(m_node, tip_before);
    BOOST_CHECK(block_template->getCoinbaseTx()->GetWitnessHash() == original_witness);
    BOOST_CHECK(block_template->getBlock().m_checked_witness_commitment);

    const CBlock valid{SolvedSubmission(*block_template, original, params)};
    BOOST_CHECK(block_template->submitSolution(valid.nVersion, valid.nTime, valid.nNonce, original));
    const TipSnapshot tip_after{ActiveTip(m_node)};
    BOOST_CHECK_EQUAL(tip_after.height, tip_before.height + 1);
    BOOST_CHECK(tip_after.hash == valid.GetHash());
    RequireIndependentBlockValid(m_node, valid.GetHash(), original);
    BOOST_CHECK(block_template->getCoinbaseTx()->GetWitnessHash() == original_witness);

    // A later, unrelated template can still be solved and activated.
    std::unique_ptr<BlockTemplate> next_template{FreshTemplate(m_node)};
    const CTransactionRef next_coinbase{next_template->getCoinbaseTx()};
    const CBlock next_block{SolvedSubmission(*next_template, next_coinbase, params)};
    BOOST_CHECK(next_template->submitSolution(next_block.nVersion, next_block.nTime, next_block.nNonce, next_coinbase));
    const TipSnapshot next_tip{ActiveTip(m_node)};
    BOOST_CHECK_EQUAL(next_tip.height, tip_after.height + 1);
    BOOST_CHECK(next_tip.hash == next_block.GetHash());
    RequireIndependentBlockValid(m_node, next_block.GetHash(), next_coinbase);
}

BOOST_AUTO_TEST_CASE(submit_solution_wrong_witness_reserved_value)
{
    const Consensus::Params& params{Assert(m_node.chainman)->GetConsensus()};
    std::unique_ptr<BlockTemplate> block_template{FreshTemplate(m_node)};
    const CTransactionRef original{block_template->getCoinbaseTx()};
    // Keep the commitment output. Only the reserved value disagrees with it.
    // The transaction id, and therefore the merkle root, stays the same.
    const CTransactionRef wrong_witness{WithReservedValue(original, 0xff)};
    BOOST_CHECK(wrong_witness->GetHash() == original->GetHash());
    BOOST_CHECK(wrong_witness->GetWitnessHash() != original->GetWitnessHash());

    const TipSnapshot tip_before{ActiveTip(m_node)};
    const CBlock invalid{SolvedSubmission(*block_template, wrong_witness, params)};
    BOOST_CHECK(invalid.hashMerkleRoot == BlockMerkleRoot(invalid));
    BOOST_CHECK(!block_template->submitSolution(invalid.nVersion, invalid.nTime, invalid.nNonce, wrong_witness));
    RequireTipUnchanged(m_node, tip_before);
    {
        LOCK(::cs_main);
        const CBlockIndex* index{Assert(m_node.chainman)->m_blockman.LookupBlockIndex(invalid.GetHash())};
        if (index) {
            BOOST_CHECK(!(index->nStatus & BLOCK_HAVE_DATA));
            BOOST_CHECK(!Assert(m_node.chainman)->ActiveChain().Contains(index));
        }
    }
}

BOOST_AUTO_TEST_CASE(submit_solution_stale_merkle_cache)
{
    const Consensus::Params& params{Assert(m_node.chainman)->GetConsensus()};
    auto stored_template = std::make_shared<node::CBlockTemplate>();
    stored_template->block = FreshTemplate(m_node)->getBlock();
    CBlock& template_block{stored_template->block};
    template_block.hashMerkleRoot = BlockMerkleRoot(template_block);
    SolveHeader(template_block, params);

    BlockValidationState state;
    BOOST_REQUIRE(CheckBlock(template_block, state, params));
    BOOST_REQUIRE(template_block.m_checked_merkle_root);
    BOOST_REQUIRE(template_block.fChecked);
    BOOST_REQUIRE(template_block.m_checked_witness_commitment);

    CMutableTransaction extra;
    extra.vin.resize(1);
    extra.vin[0].prevout.hash = Txid::FromUint256(uint256::ONE);
    extra.vin[0].prevout.n = 0;
    extra.vin[0].scriptSig = CScript() << OP_TRUE;
    extra.vout.emplace_back(1, CScript() << OP_TRUE);
    const CTransactionRef extra_tx{MakeTransactionRef(std::move(extra))};
    // Three identical non-coinbase transactions make the merkle tree mutated
    // even after the root is recomputed. No other CheckBlock failure is required
    // for that result, so a stale m_checked_merkle_root or fChecked flag would
    // let the header be stored.
    template_block.vtx.push_back(extra_tx);
    template_block.vtx.push_back(extra_tx);
    template_block.vtx.push_back(extra_tx);
    BOOST_REQUIRE(template_block.m_checked_merkle_root);
    BOOST_REQUIRE(template_block.fChecked);

    const CTransactionRef coinbase{template_block.vtx[0]};
    auto block_template = interfaces::MakeBlockTemplate(std::move(stored_template), m_node);
    const TipSnapshot tip_before{ActiveTip(m_node)};
    const CBlock invalid{SolvedSubmission(*block_template, coinbase, params)};
    bool mutated{false};
    BOOST_CHECK(BlockMerkleRoot(invalid, &mutated) == invalid.hashMerkleRoot);
    BOOST_CHECK(mutated);
    BOOST_CHECK(!block_template->submitSolution(invalid.nVersion, invalid.nTime, invalid.nNonce, coinbase));
    RequireTipUnchanged(m_node, tip_before);
    {
        LOCK(::cs_main);
        BOOST_CHECK(Assert(m_node.chainman)->m_blockman.LookupBlockIndex(invalid.GetHash()) == nullptr);
    }
}

BOOST_AUTO_TEST_CASE(regenerate_commitments_clears_cached_checks)
{
    const Consensus::Params& params{Assert(m_node.chainman)->GetConsensus()};
    std::unique_ptr<BlockTemplate> block_template{FreshTemplate(m_node)};
    CBlock block{block_template->getBlock()};
    BOOST_REQUIRE(block.m_checked_witness_commitment);

    block.hashMerkleRoot = BlockMerkleRoot(block);
    SolveHeader(block, params);
    BlockValidationState checked;
    BOOST_REQUIRE(CheckBlock(block, checked, params));
    BOOST_REQUIRE(block.m_checked_witness_commitment);
    BOOST_REQUIRE(block.m_checked_merkle_root);
    BOOST_REQUIRE(block.fChecked);

    CMutableTransaction extra;
    extra.vin.resize(1);
    extra.vin[0].prevout.hash = Txid::FromUint256(uint256::ONE);
    extra.vin[0].prevout.n = 1;
    extra.vout.emplace_back(1, CScript() << OP_TRUE);
    block.vtx.push_back(MakeTransactionRef(std::move(extra)));
    // The appended transaction makes the cached commitment and merkle result stale.
    BOOST_REQUIRE(block.m_checked_witness_commitment);
    BOOST_REQUIRE(block.m_checked_merkle_root);
    BOOST_REQUIRE(block.fChecked);

    node::RegenerateCommitments(block, *Assert(m_node.chainman));
    BOOST_CHECK(!block.m_checked_witness_commitment);
    BOOST_CHECK(!block.m_checked_merkle_root);
    BOOST_CHECK(!block.fChecked);

    // Validate a copy so this call cannot repopulate the flags on `block`.
    CBlock regenerated{block};
    BOOST_CHECK(!IsBlockMutated(regenerated, /*check_witness_root=*/true));

    CMutableTransaction corrupted{*block.vtx[0]};
    corrupted.vin[0].scriptWitness.stack.assign(1, std::vector<unsigned char>(32, 0x11));
    block.vtx[0] = MakeTransactionRef(std::move(corrupted));
    BOOST_CHECK(IsBlockMutated(block, /*check_witness_root=*/true));
}

BOOST_AUTO_TEST_SUITE_END()
