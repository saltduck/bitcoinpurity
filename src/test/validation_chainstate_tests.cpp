// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
#include <chainparams.h>
#include <consensus/validation.h>
#include <consensus/merkle.h>
#include <pow.h>
#include <script/script.h>
#include <node/kernel_notifications.h>
#include <random.h>
#include <rpc/blockchain.h>
#include <sync.h>
#include <test/util/chainstate.h>
#include <test/util/coins.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/check.h>
#include <util/mempressure.h>
#include <validation.h>

#include <algorithm>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(validation_chainstate_tests, ChainTestingSetup)

//! Test resizing coins-related Chainstate caches during runtime.
//!
BOOST_AUTO_TEST_CASE(validation_chainstate_resize_caches)
{
    g_low_memory_threshold = 0;  // disable to get deterministic flushing

    ChainstateManager& manager = *Assert(m_node.chainman);
    CTxMemPool& mempool = *Assert(m_node.mempool);
    Chainstate& c1 = WITH_LOCK(cs_main, return manager.InitializeChainstate(&mempool));
    c1.InitCoinsDB(
        /*cache_size_bytes=*/1 << 23, /*in_memory=*/true, /*should_wipe=*/false);
    WITH_LOCK(::cs_main, c1.InitCoinsCache(1 << 23));
    BOOST_REQUIRE(c1.LoadGenesisBlock()); // Need at least one block loaded to be able to flush caches

    // Add a coin to the in-memory cache, upsize once, then downsize.
    {
        LOCK(::cs_main);
        const auto outpoint = AddTestCoin(m_rng, c1.CoinsTip());

        // Set a meaningless bestblock value in the coinsview cache - otherwise we won't
        // flush during ResizecoinsCaches() and will subsequently hit an assertion.
        c1.CoinsTip().SetBestBlock(m_rng.rand256());

        BOOST_CHECK(c1.CoinsTip().HaveCoinInCache(outpoint));

        c1.ResizeCoinsCaches(
            1 << 24,  // upsizing the coinsview cache
            1 << 22  // downsizing the coinsdb cache
        );

        // View should still have the coin cached, since we haven't destructed the cache on upsize.
        BOOST_CHECK(c1.CoinsTip().HaveCoinInCache(outpoint));

        c1.ResizeCoinsCaches(
            1 << 22,  // downsizing the coinsview cache
            1 << 23  // upsizing the coinsdb cache
        );

        // The view cache should be empty since we had to destruct to downsize.
        BOOST_CHECK(!c1.CoinsTip().HaveCoinInCache(outpoint));
    }
}

//! Test UpdateTip behavior for both active and background chainstates.
//!
//! When run on the background chainstate, UpdateTip should do a subset
//! of what it does for the active chainstate.
BOOST_FIXTURE_TEST_CASE(chainstate_update_tip, TestChain100Setup)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    const auto get_notify_tip{[&]() {
        LOCK(m_node.notifications->m_tip_block_mutex);
        BOOST_REQUIRE(m_node.notifications->TipBlock());
        return *m_node.notifications->TipBlock();
    }};
    uint256 curr_tip = get_notify_tip();

    // Mine 10 more blocks, putting at us height 110 where a valid assumeutxo value can
    // be found.
    mineBlocks(10);

    // After adding some blocks to the tip, best block should have changed.
    BOOST_CHECK(get_notify_tip() != curr_tip);

    // Grab block 1 from disk; we'll add it to the background chain later.
    std::shared_ptr<CBlock> pblockone = std::make_shared<CBlock>();
    {
        LOCK(::cs_main);
        chainman.m_blockman.ReadBlock(*pblockone, *chainman.ActiveChain()[1]);
    }

    BOOST_REQUIRE(CreateAndActivateUTXOSnapshot(
        this, NoMalleation, /*reset_chainstate=*/ true));

    // Ensure our active chain is the snapshot chainstate.
    BOOST_CHECK(WITH_LOCK(::cs_main, return chainman.IsSnapshotActive()));

    curr_tip = get_notify_tip();

    // Mine a new block on top of the activated snapshot chainstate.
    mineBlocks(1);  // Defined in TestChain100Setup.

    // After adding some blocks to the snapshot tip, best block should have changed.
    BOOST_CHECK(get_notify_tip() != curr_tip);

    curr_tip = get_notify_tip();

    BOOST_CHECK_EQUAL(chainman.GetAll().size(), 2);

    Chainstate& background_cs{*Assert([&]() -> Chainstate* {
        for (Chainstate* cs : chainman.GetAll()) {
            if (cs != &chainman.ActiveChainstate()) {
                return cs;
            }
        }
        return nullptr;
    }())};

    // Append the first block to the background chain.
    BlockValidationState state;
    CBlockIndex* pindex = nullptr;
    const CChainParams& chainparams = Params();
    bool newblock = false;

    // TODO: much of this is inlined from ProcessNewBlock(); just reuse PNB()
    // once it is changed to support multiple chainstates.
    {
        LOCK(::cs_main);
        bool checked = CheckBlock(*pblockone, state, chainparams.GetConsensus());
        BOOST_CHECK(checked);
        bool accepted = chainman.AcceptBlock(
            pblockone, state, &pindex, true, nullptr, &newblock, true);
        BOOST_CHECK(accepted);
    }

    // UpdateTip is called here
    bool block_added = background_cs.ActivateBestChain(state, pblockone);

    // Ensure tip is as expected
    BOOST_CHECK_EQUAL(background_cs.m_chain.Tip()->GetBlockHash(), pblockone->GetHash());

    // get_notify_tip() should be unchanged after adding a block to the background
    // validation chain.
    BOOST_CHECK(block_added);
    BOOST_CHECK_EQUAL(curr_tip, get_notify_tip());
}


struct DeepReorgTestingSetup : TestChain100Setup {
    DeepReorgTestingSetup(const char* depth = "-parkreorgdepth=6", bool enabled = true)
        : TestChain100Setup(ChainType::REGTEST, {.extra_args={
              enabled ? "-parkdeepreorg=1" : "-parkdeepreorg=0",
              depth}}) {}

    std::pair<CBlockIndex*, CBlockIndex*> AcceptFork(int rewind, bool ancestor_last = false, CBlock* withheld = nullptr)
    {
        auto& chainman = *m_node.chainman;
        auto& chainstate = chainman.ActiveChainstate();
        auto* prev = WITH_LOCK(cs_main, return chainstate.m_chain.Tip()->GetAncestor(100 - rewind));
        std::vector<CBlock> blocks;
        for (int height = prev->nHeight + 1; height <= 101; ++height) {
            CBlock block = CreateBlock({}, CScript() << OP_TRUE, chainstate);
            block.hashPrevBlock = prev->GetBlockHash();
            block.nTime = prev->GetBlockTime() + (ancestor_last ? 2 : 1);
            CMutableTransaction coinbase(*block.vtx[0]);
            coinbase.vin[0].scriptSig = CScript() << height << OP_0;
            block.vtx[0] = MakeTransactionRef(coinbase);
            block.hashMerkleRoot = BlockMerkleRoot(block);
            block.nNonce = 0;
            while (!CheckProofOfWork(block.GetHash(), block.nBits, chainman.GetConsensus())) ++block.nNonce;
            BlockValidationState state;
            const CBlockHeader header = block;
            BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({&header, 1}, true, state));
            prev = WITH_LOCK(cs_main, return chainman.m_blockman.LookupBlockIndex(block.GetHash()));
            blocks.push_back(std::move(block));
        }
        auto* first = WITH_LOCK(cs_main, return chainman.m_blockman.LookupBlockIndex(blocks.front().GetHash()));
        if (ancestor_last) std::rotate(blocks.begin(), blocks.begin() + 1, blocks.end());
        for (const auto& block : blocks) {
            if (withheld && block.GetHash() == first->GetBlockHash()) {
                *withheld = block;
                continue;
            }
            BlockValidationState state;
            LOCK(cs_main);
            BOOST_REQUIRE_MESSAGE(chainman.AcceptBlock(std::make_shared<const CBlock>(block), state, nullptr, true, nullptr, nullptr, true), state.ToString());
            // Acceptance must park before candidate propagation, without ABC.
            if (chainman.m_options.park_deep_reorg && rewind > chainman.m_options.park_reorg_depth) {
                BOOST_CHECK(first->nStatus & BLOCK_PARKED);
                BOOST_CHECK(!chainstate.setBlockIndexCandidates.contains(prev));
            }
        }
        return {first, prev};
    }

    // Adapted from ABC/BCHN abc[-_feature_]parkedchain.py state orderings:
    // 4a9b35aa30806798214b5d4e33c577a4f16d9df5 and
    // 3cc9d160357adca8e001539f45385197ccae4954. Use an inactive branch to
    // isolate state independence from Purity's active-park RPC restriction.
    void CheckStateOrder(std::initializer_list<char> actions)
    {
        auto& chainstate = m_node.chainman->ActiveChainstate();
        auto* active_tip = WITH_LOCK(cs_main, return chainstate.m_chain.Tip());
        auto [root, tip] = AcceptFork(7);
        WITH_LOCK(cs_main, chainstate.UnparkBlock(root));
        bool failed{false};
        bool parked{false};
        for (char action : actions) {
            BlockValidationState state;
            switch (action) {
            case 'i':
                BOOST_REQUIRE(chainstate.InvalidateBlock(state, root));
                failed = true;
                break;
            case 'p':
                BOOST_REQUIRE(chainstate.ParkBlock(state, root));
                parked = true;
                break;
            case 'u':
                WITH_LOCK(cs_main, chainstate.UnparkBlock(root));
                parked = false;
                break;
            case 'r':
                WITH_LOCK(cs_main, chainstate.ResetBlockFailureFlags(root));
                failed = false;
                break;
            }
            {
                LOCK(cs_main);
                BOOST_CHECK_EQUAL(bool(root->nStatus & BLOCK_FAILED_MASK), failed);
                BOOST_CHECK_EQUAL(bool(root->nStatus & BLOCK_PARKED_MASK), parked);
                BOOST_CHECK_EQUAL(root->IsValid(BLOCK_VALID_TRANSACTIONS), !failed);
            }
            BOOST_REQUIRE(chainstate.ActivateBestChain(state, nullptr));
            BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.m_chain.Tip()) == (failed || parked ? active_tip : tip));
        }
    }

};

struct CustomDepthTestingSetup : DeepReorgTestingSetup {
    CustomDepthTestingSetup() : DeepReorgTestingSetup("-parkreorgdepth=4") {}
};

struct ParkingDisabledTestingSetup : DeepReorgTestingSetup {
    ParkingDisabledTestingSetup() : DeepReorgTestingSetup("-parkreorgdepth=6", false) {}
};

BOOST_FIXTURE_TEST_CASE(deep_reorg_acceptance_threshold, DeepReorgTestingSetup)
{
    // ABC/BCHN deep-reorg scenarios, adapted to Purity's strict depth-6 rule.
    for (int rewind : {1, 2, 5, 6, 7, 8, 20}) {
        auto [root, tip] = AcceptFork(rewind);
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(bool(root->nStatus & BLOCK_PARKED), rewind > 6);
        BOOST_CHECK_EQUAL(bool(tip->nStatus & BLOCK_PARKED_MASK), rewind > 6);
        BOOST_CHECK_EQUAL(m_node.chainman->ActiveChainstate().setBlockIndexCandidates.contains(tip), rewind <= 6);
        BOOST_CHECK(!(root->nStatus & BLOCK_FAILED_MASK));
    }
}

BOOST_FIXTURE_TEST_CASE(deep_reorg_custom_acceptance_threshold, CustomDepthTestingSetup)
{
    BOOST_CHECK_EQUAL(m_node.chainman->m_options.park_reorg_depth, 4);
    for (int rewind : {4, 5}) {
        auto [root, tip] = AcceptFork(rewind);
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(bool(root->nStatus & BLOCK_PARKED), rewind > 4);
        BOOST_CHECK_EQUAL(bool(tip->nStatus & BLOCK_PARKED_MASK), rewind > 4);
    }
}

BOOST_FIXTURE_TEST_CASE(deep_reorg_acceptance_disabled, ParkingDisabledTestingSetup)
{
    auto [root, tip] = AcceptFork(20, true);
    BOOST_CHECK(WITH_LOCK(cs_main, return !(root->nStatus & BLOCK_PARKED_MASK)));
    BlockValidationState state;
    BOOST_REQUIRE(m_node.chainman->ActiveChainstate().ActivateBestChain(state));
    BOOST_CHECK(WITH_LOCK(cs_main, return m_node.chainman->ActiveChainstate().m_chain.Tip()) == tip);
}

BOOST_FIXTURE_TEST_CASE(deep_reorg_null_block_and_unpark, DeepReorgTestingSetup)
{
    auto& chainstate = m_node.chainman->ActiveChainstate();
    auto* original_tip = WITH_LOCK(cs_main, return chainstate.m_chain.Tip());
    auto [first, tip] = AcceptFork(7, true);
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.ActivateBestChain(state, nullptr));
    {
        LOCK(cs_main);
        BOOST_CHECK(chainstate.m_chain.Tip() == original_tip);
        BOOST_CHECK(first->nStatus & BLOCK_PARKED);
        BOOST_CHECK(tip->nStatus & BLOCK_PARKED_CHILD);
        chainstate.TryAddBlockIndexCandidate(first);
        chainstate.TryAddBlockIndexCandidate(tip);
        BOOST_CHECK(!chainstate.setBlockIndexCandidates.contains(first));
        BOOST_CHECK(!chainstate.setBlockIndexCandidates.contains(tip));
        // Reprocessing a parked block does not clear its status.
        CBlock block;
        BOOST_REQUIRE(m_node.chainman->m_blockman.ReadBlock(block, *first));
        BOOST_REQUIRE(m_node.chainman->AcceptBlock(std::make_shared<const CBlock>(block), state, nullptr, true, nullptr, nullptr, true));
        BOOST_CHECK(first->nStatus & BLOCK_PARKED);
        chainstate.UnparkBlock(first);
        BOOST_CHECK(!(first->nStatus & BLOCK_PARKED_MASK));
        BOOST_CHECK(!(tip->nStatus & BLOCK_PARKED_MASK));
        BOOST_CHECK(chainstate.setBlockIndexCandidates.contains(tip));
    }
    // Normal null-body activation must honor manual unpark without an override.
    BOOST_REQUIRE(chainstate.ActivateBestChain(state, nullptr));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.m_chain.Tip()) == tip);
}

BOOST_FIXTURE_TEST_CASE(deep_reorg_delivery_order, DeepReorgTestingSetup)
{
    for (bool ancestor_last : {false, true}) {
        auto [root, tip] = AcceptFork(7, ancestor_last);
        LOCK(cs_main);
        BOOST_CHECK(root->nStatus & BLOCK_PARKED);
        BOOST_CHECK(tip->nStatus & BLOCK_PARKED_CHILD);
        BOOST_CHECK(!m_node.chainman->ActiveChainstate().setBlockIndexCandidates.contains(tip));
    }
}

BOOST_FIXTURE_TEST_CASE(deep_reorg_unpark_missing_body, DeepReorgTestingSetup)
{
    auto& chainman = *m_node.chainman;
    auto& chainstate = chainman.ActiveChainstate();
    CBlock missing;
    auto [root, tip] = AcceptFork(7, true, &missing);
    BlockValidationState state;
    // Manual park of an unlinked descendant must also be safe to clear.
    BOOST_REQUIRE(chainstate.ParkBlock(state, tip));
    {
        LOCK(cs_main);
        BOOST_REQUIRE(root->nStatus & BLOCK_PARKED);
        BOOST_REQUIRE(!(root->nStatus & BLOCK_HAVE_DATA));
        chainstate.UnparkBlock(root);
        // A parked header may be cleared, but is not a block-data candidate.
        BOOST_REQUIRE(!chainstate.setBlockIndexCandidates.contains(root));
        BOOST_REQUIRE(!chainstate.setBlockIndexCandidates.contains(tip));
    }
    BOOST_REQUIRE(chainstate.ActivateBestChain(state, nullptr));
    BOOST_CHECK_EQUAL(WITH_LOCK(cs_main, return chainstate.m_chain.Height()), 100);
    {
        LOCK(cs_main);
        // Newly accepted data is evaluated again against the current active tip.
        BOOST_REQUIRE(chainman.AcceptBlock(std::make_shared<const CBlock>(missing), state, nullptr, true, nullptr, nullptr, true));
        BOOST_REQUIRE(root->nStatus & BLOCK_PARKED);
        BOOST_REQUIRE(!chainstate.setBlockIndexCandidates.contains(tip));
        chainstate.UnparkBlock(root);
    }
    BOOST_REQUIRE(chainstate.ActivateBestChain(state, nullptr));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainstate.m_chain.Tip()) == tip);
}

BOOST_FIXTURE_TEST_CASE(deep_reorg_active_body_redownload, DeepReorgTestingSetup)
{
    auto& chainman = *m_node.chainman;
    auto& chainstate = chainman.ActiveChainstate();
    LOCK(cs_main);
    CBlockIndex* ancestor = chainstate.m_chain[80];
    CBlock body;
    BOOST_REQUIRE(chainman.m_blockman.ReadBlock(body, *ancestor));
    // Model the metadata left by pruning before accepting an active-chain body.
    ancestor->nStatus &= ~(BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);
    ancestor->nFile = 0;
    ancestor->nDataPos = 0;
    ancestor->nUndoPos = 0;
    chainman.m_blockman.m_have_pruned = true;
    BlockValidationState state;
    BOOST_REQUIRE(chainman.AcceptBlock(std::make_shared<const CBlock>(body), state, nullptr, true, nullptr, nullptr, true));
    BOOST_CHECK(!(ancestor->nStatus & BLOCK_PARKED_MASK));
    BOOST_CHECK(ancestor->nStatus & BLOCK_HAVE_DATA);
    BOOST_CHECK_EQUAL(chainstate.m_chain.Height(), 100);
}

BOOST_FIXTURE_TEST_CASE(parked_state_order_a, DeepReorgTestingSetup)
{
    CheckStateOrder({'i', 'p', 'u', 'r'});
}

BOOST_FIXTURE_TEST_CASE(parked_state_order_b, DeepReorgTestingSetup)
{
    CheckStateOrder({'p', 'i', 'r', 'u'});
}

BOOST_FIXTURE_TEST_CASE(parked_state_order_c, DeepReorgTestingSetup)
{
    CheckStateOrder({'i', 'p', 'r', 'u'});
}

BOOST_FIXTURE_TEST_CASE(parked_state_order_d, DeepReorgTestingSetup)
{
    CheckStateOrder({'p', 'i', 'u', 'r'});
}

// Marker invariant described by the ABC/BCHN fixes
// 638c2fbd9748e9f7f7fddaba068b9f2b835695af and
// 6cc62f82f9e17d6d4cebb649d959de9188017ba9 (neither added a new test).
BOOST_FIXTURE_TEST_CASE(parked_root_child_markers, DeepReorgTestingSetup)
{
    auto [root, tip] = AcceptFork(7);
    LOCK(cs_main);
    BOOST_CHECK_EQUAL(root->nStatus & BLOCK_PARKED_MASK, BLOCK_PARKED);
    for (auto* child = tip; child != root; child = child->pprev) {
        BOOST_CHECK_EQUAL(child->nStatus & BLOCK_PARKED_MASK, BLOCK_PARKED_CHILD);
    }
    BOOST_CHECK(!(root->nStatus & BLOCK_FAILED_MASK));
    BOOST_CHECK(!(tip->nStatus & BLOCK_FAILED_MASK));
}

BOOST_AUTO_TEST_SUITE_END()
