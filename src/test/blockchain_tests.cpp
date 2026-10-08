// Copyright (c) 2017-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chain.h>
#include <consensus/merkle.h>
#include <node/blockstorage.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <util/string.h>
#include <validation.h>

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

using util::ToString;

/* Equality between doubles is imprecise. Comparison should be done
 * with a small threshold of tolerance, rather than exact equality.
 */
static bool DoubleEquals(double a, double b, double epsilon)
{
    return std::abs(a - b) < epsilon;
}

static CBlockIndex* CreateBlockIndexWithNbits(uint32_t nbits)
{
    CBlockIndex* block_index = new CBlockIndex();
    block_index->nHeight = 46367;
    block_index->nTime = 1269211443;
    block_index->nBits = nbits;
    return block_index;
}

static void RejectDifficultyMismatch(double difficulty, double expected_difficulty) {
     BOOST_CHECK_MESSAGE(
        DoubleEquals(difficulty, expected_difficulty, 0.00001),
        "Difficulty was " + ToString(difficulty)
            + " but was expected to be " + ToString(expected_difficulty));
}

/* Given a BlockIndex with the provided nbits,
 * verify that the expected difficulty results.
 */
static void TestDifficulty(uint32_t nbits, double expected_difficulty)
{
    CBlockIndex* block_index = CreateBlockIndexWithNbits(nbits);
    double difficulty = GetDifficulty(*block_index);
    delete block_index;

    RejectDifficultyMismatch(difficulty, expected_difficulty);
}

BOOST_FIXTURE_TEST_SUITE(blockchain_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(get_difficulty_for_very_low_target)
{
    TestDifficulty(0x1f111111, 0.000001);
}

BOOST_AUTO_TEST_CASE(get_difficulty_for_low_target)
{
    TestDifficulty(0x1ef88f6f, 0.000016);
}

BOOST_AUTO_TEST_CASE(get_difficulty_for_mid_target)
{
    TestDifficulty(0x1df88f6f, 0.004023);
}

BOOST_AUTO_TEST_CASE(get_difficulty_for_high_target)
{
    TestDifficulty(0x1cf88f6f, 1.029916);
}

BOOST_AUTO_TEST_CASE(get_difficulty_for_very_high_target)
{
    TestDifficulty(0x12345678, 5913134931067755359633408.0);
}

//! Prune chain from height down to genesis block and check that
//! GetPruneHeight returns the correct value
static void CheckGetPruneHeight(node::BlockManager& blockman, CChain& chain, int height) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    AssertLockHeld(::cs_main);

    // Emulate pruning all blocks from `height` down to the genesis block
    // by unsetting the `BLOCK_HAVE_DATA` flag from `nStatus`
    for (CBlockIndex* it{chain[height]}; it != nullptr && it->nHeight > 0; it = it->pprev) {
        it->nStatus &= ~BLOCK_HAVE_DATA;
    }

    const auto prune_height{GetPruneHeight(blockman, chain)};
    BOOST_REQUIRE(prune_height.has_value());
    BOOST_CHECK_EQUAL(*prune_height, height);
}

BOOST_FIXTURE_TEST_CASE(get_prune_height, TestChain100Setup)
{
    LOCK(::cs_main);
    auto& chain = m_node.chainman->ActiveChain();
    auto& blockman = m_node.chainman->m_blockman;

    // Fresh chain of 100 blocks without any pruned blocks, so std::nullopt should be returned
    BOOST_CHECK(!GetPruneHeight(blockman, chain).has_value());

    // Start pruning
    CheckGetPruneHeight(blockman, chain, 1);
    CheckGetPruneHeight(blockman, chain, 99);
    CheckGetPruneHeight(blockman, chain, 100);
}

BOOST_AUTO_TEST_CASE(num_chain_tx_max)
{
    CBlockIndex block_index{};
    block_index.m_chain_tx_count = std::numeric_limits<uint64_t>::max();
    BOOST_CHECK_EQUAL(block_index.m_chain_tx_count, std::numeric_limits<uint64_t>::max());
}

BOOST_FIXTURE_TEST_CASE(invalidate_block, TestChain100Setup)
{
    auto& chainman = *m_node.chainman;
    auto& chainstate = chainman.ActiveChainstate();
    auto& blockman = chainman.m_blockman;
    std::vector<std::pair<CBlockIndex*, uint32_t>> original;
    {
        LOCK(cs_main);
        for (auto* block = chainstate.m_chain.Tip(); block; block = block->pprev) {
            original.emplace_back(block, block->nStatus);
        }
        BOOST_REQUIRE(blockman.WriteBlockIndexDB());
    }
    auto* tip = original.front().first;
    auto* target = tip->GetAncestor(tip->nHeight - 10);
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, target));
    BOOST_REQUIRE(state.IsValid());
    {
        LOCK(cs_main);
        BOOST_CHECK(chainstate.m_chain.Tip() == target->pprev);
        for (const auto& [block, status] : original) {
            const uint32_t failure = block->nHeight > target->nHeight ? BLOCK_FAILED_CHILD :
                block == target ? BLOCK_FAILED_VALID : 0;
            BOOST_CHECK_EQUAL(block->nStatus, status | failure);
        }
    }
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, tip));
    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(tip->nStatus & BLOCK_FAILED_MASK, BLOCK_FAILED_CHILD);
        BOOST_CHECK(!chainman.m_failed_blocks.contains(tip));
        BOOST_REQUIRE(blockman.WriteBlockIndexDB());
        node::BlockMap reloaded;
        BOOST_REQUIRE(blockman.m_block_tree_db->LoadBlockIndexGuts(chainman.GetConsensus(), [&](const uint256& hash) -> CBlockIndex* {
            if (hash.IsNull()) return nullptr;
            auto [it, inserted] = reloaded.try_emplace(hash);
            it->second.phashBlock = &it->first;
            return &it->second;
        }, m_interrupt));
        for (const auto& [block, status] : original) {
            BOOST_CHECK_EQUAL(reloaded.at(block->GetBlockHash()).nStatus, block->nStatus);
        }
        chainstate.ResetBlockFailureFlags(target);
        for (const auto& [block, status] : original) {
            BOOST_CHECK_EQUAL(block->nStatus, status);
            BOOST_CHECK(!chainman.m_failed_blocks.contains(block));
        }
        BOOST_CHECK(chainstate.setBlockIndexCandidates.contains(tip));
    }
    BOOST_REQUIRE(chainstate.ActivateBestChain(state));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.m_chain.Tip()) == tip);
}

struct FailureFlagsTestingSetup : TestChain100Setup {
    std::pair<CBlockIndex*, CBlockIndex*> AcceptFork()
    {
        auto& chainman = *m_node.chainman;
        auto& chainstate = chainman.ActiveChainstate();
        auto* prev = WITH_LOCK(cs_main, return chainstate.m_chain[95]);
        CBlockIndex* root{nullptr};
        for (int height = 96; height <= 102; ++height) {
            CBlock block = CreateBlock({}, CScript() << OP_TRUE, chainstate);
            block.hashPrevBlock = prev->GetBlockHash();
            block.nTime = prev->GetBlockTime() + 1;
            CMutableTransaction coinbase(*block.vtx[0]);
            coinbase.vin[0].scriptSig = CScript() << height << OP_0;
            block.vtx[0] = MakeTransactionRef(coinbase);
            block.hashMerkleRoot = BlockMerkleRoot(block);
            block.nNonce = 0;
            while (!CheckProofOfWork(block.GetHash(), block.nBits, chainman.GetConsensus())) ++block.nNonce;
            BlockValidationState state;
            LOCK(cs_main);
            BOOST_REQUIRE(chainman.AcceptBlock(std::make_shared<const CBlock>(block), state, &prev, true, nullptr, nullptr, true));
            if (!root) root = prev;
        }
        return {root, prev};
    }
};

BOOST_FIXTURE_TEST_CASE(invalidate_inactive_block, FailureFlagsTestingSetup)
{
    auto& chainman = *m_node.chainman;
    auto& chainstate = chainman.ActiveChainstate();
    auto [root, tip] = AcceptFork();
    auto* active_tip = WITH_LOCK(cs_main, return chainstate.m_chain.Tip());
    std::vector<std::pair<CBlockIndex*, uint32_t>> original;
    {
        LOCK(cs_main);
        for (auto* block = tip; block != root->pprev; block = block->pprev) {
            original.emplace_back(block, block->nStatus);
        }
        chainstate.SetBlockFailureFlags(root);
        BOOST_CHECK_EQUAL(root->nStatus & BLOCK_FAILED_MASK, 0);
        chainstate.ResetBlockFailureFlags(root);
        BOOST_REQUIRE(chainman.m_blockman.WriteBlockIndexDB());
    }
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, tip));
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return tip->nStatus & BLOCK_FAILED_MASK), BLOCK_FAILED_VALID);
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, root));
    {
        LOCK(cs_main);
        BOOST_CHECK(chainstate.m_chain.Tip() == active_tip);
        BOOST_CHECK_EQUAL(root->nStatus & BLOCK_FAILED_MASK, BLOCK_FAILED_VALID);
        for (auto* block = tip; block != root; block = block->pprev) {
            BOOST_CHECK_EQUAL(block->nStatus & BLOCK_FAILED_MASK, BLOCK_FAILED_CHILD);
        }
        BOOST_REQUIRE(chainman.m_blockman.WriteBlockIndexDB());
        for (auto* block = tip; block != root->pprev; block = block->pprev) {
            CDiskBlockIndex disk_index;
            BOOST_REQUIRE(chainman.m_blockman.m_block_tree_db->Read(std::make_pair(uint8_t{'b'}, block->GetBlockHash()), disk_index));
            BOOST_CHECK_EQUAL(disk_index.nStatus, block->nStatus);
        }
        BOOST_CHECK_EQUAL(root->pprev->nStatus & BLOCK_FAILED_MASK, 0);
        for (const auto& [block, status] : original) {
            BOOST_CHECK_EQUAL(block->nStatus & ~BLOCK_FAILED_MASK, status);
        }
        chainstate.ResetBlockFailureFlags(root);
        BOOST_CHECK(chainstate.setBlockIndexCandidates.contains(tip));
    }
    BOOST_REQUIRE(chainstate.ActivateBestChain(state));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.m_chain.Tip()) == tip);
}

BOOST_FIXTURE_TEST_CASE(invalidate_parked_branch, FailureFlagsTestingSetup)
{
    auto& chainstate = m_node.chainman->ActiveChainstate();
    auto [root, tip] = AcceptFork();
    auto* active_tip = WITH_LOCK(cs_main, return chainstate.m_chain.Tip());
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.ParkBlock(state, root));
    BOOST_REQUIRE(chainstate.ActivateBestChain(state));
    {
        LOCK(cs_main);
        BOOST_REQUIRE(root->nStatus & BLOCK_PARKED);
        BOOST_REQUIRE(tip->nStatus & BLOCK_PARKED_CHILD);
        BOOST_CHECK_EQUAL(root->nStatus & BLOCK_FAILED_MASK, 0);
        BOOST_CHECK_EQUAL(tip->nStatus & BLOCK_FAILED_MASK, 0);
    }
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, active_tip->GetAncestor(96)));
    {
        LOCK(cs_main);
        for (auto* block = tip; block != root->pprev; block = block->pprev) {
            BOOST_CHECK(!chainstate.setBlockIndexCandidates.contains(block));
            BOOST_CHECK(block->nStatus & BLOCK_PARKED_MASK);
        }
        chainstate.ResetBlockFailureFlags(active_tip->GetAncestor(96));
    }
    BOOST_REQUIRE(chainstate.ActivateBestChain(state));
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, tip));
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, root));
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, tip));
    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(root->nStatus & BLOCK_FAILED_MASK, BLOCK_FAILED_VALID);
        BOOST_CHECK_EQUAL(tip->nStatus & BLOCK_FAILED_MASK, BLOCK_FAILED_CHILD);
        chainstate.ResetBlockFailureFlags(root);
        for (auto* block = tip; block != root->pprev; block = block->pprev) {
            BOOST_CHECK_EQUAL(block->nStatus & BLOCK_FAILED_MASK, 0);
            BOOST_CHECK(block->nStatus & BLOCK_PARKED_MASK);
            BOOST_CHECK(!chainstate.setBlockIndexCandidates.contains(block));
        }
    }
    BOOST_REQUIRE(chainstate.ActivateBestChain(state));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.m_chain.Tip()) == active_tip);
    WITH_LOCK(cs_main, chainstate.UnparkBlock(root));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.setBlockIndexCandidates.contains(tip)));
    BOOST_REQUIRE(chainstate.ActivateBestChain(state));
    {
        LOCK(cs_main);
        BOOST_CHECK(chainstate.m_chain.Tip() == tip);
        BOOST_CHECK_EQUAL(root->nStatus & BLOCK_PARKED_MASK, 0);
        BOOST_CHECK_EQUAL(tip->nStatus & BLOCK_PARKED_MASK, 0);
    }
}

BOOST_FIXTURE_TEST_CASE(invalidate_with_parked_ancestor, FailureFlagsTestingSetup)
{
    auto& chainstate = m_node.chainman->ActiveChainstate();
    auto [root, tip] = AcceptFork();
    auto* target = WITH_LOCK(cs_main, return chainstate.m_chain[96]);
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.ParkBlock(state, root));
    BOOST_REQUIRE(WITH_LOCK(cs_main, return !(tip->nStatus & BLOCK_PARKED_MASK)));
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, target));
    {
        LOCK(cs_main);
        for (auto* block = tip; block != root->pprev; block = block->pprev) {
            BOOST_CHECK(!chainstate.setBlockIndexCandidates.contains(block));
            BOOST_CHECK_EQUAL(block->nStatus & BLOCK_FAILED_MASK, 0);
        }
        BOOST_CHECK(root->nStatus & BLOCK_PARKED);
    }
}

BOOST_AUTO_TEST_SUITE_END()
