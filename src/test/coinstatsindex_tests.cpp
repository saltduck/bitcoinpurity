// Copyright (c) 2020-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <arith_uint256.h>
#include <dbwrapper.h>
#include <index/coinstatsindex.h>
#include <interfaces/chain.h>
#include <kernel/coinstats.h>
#include <node/blockstorage.h>
#include <rpc/server.h>
#include <test/util/index.h>
#include <test/util/setup_common.h>
#include <test/util/validation.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>

namespace {
// Independent wire-format fixture: catches field order, width and endian changes.
struct CoinStatsRecord {
    uint256 muhash;
    uint64_t outputs{0}, bogo_size{0};
    CAmount amount{0}, subsidy{0};
    uint256 prevout, new_outputs, coinbase;
    CAmount genesis{0}, bip30{0}, scripts{0}, unclaimed{0};

    SERIALIZE_METHODS(CoinStatsRecord, obj)
    {
        READWRITE(obj.muhash, obj.outputs, obj.bogo_size, obj.amount, obj.subsidy,
                  obj.prevout, obj.new_outputs, obj.coinbase,
                  obj.genesis, obj.bip30, obj.scripts, obj.unclaimed);
    }
};
} // namespace

BOOST_AUTO_TEST_SUITE(coinstatsindex_tests)

BOOST_FIXTURE_TEST_CASE(coinstatsindex_initial_sync, TestChain100Setup)
{
    CoinStatsIndex coin_stats_index{interfaces::MakeChain(m_node), 1 << 20, true};
    BOOST_REQUIRE(coin_stats_index.Init());

    const CBlockIndex* block_index;
    {
        LOCK(cs_main);
        block_index = m_node.chainman->ActiveChain().Tip();
    }

    // CoinStatsIndex should not be found before it is started.
    BOOST_CHECK(!coin_stats_index.LookUpStats(*block_index));

    // BlockUntilSyncedToCurrentChain should return false before CoinStatsIndex
    // is started.
    BOOST_CHECK(!coin_stats_index.BlockUntilSyncedToCurrentChain());

    BOOST_REQUIRE(coin_stats_index.StartBackgroundSync());

    IndexWaitSynced(coin_stats_index, *Assert(m_node.shutdown_signal));

    // Check that CoinStatsIndex works for genesis block.
    const CBlockIndex* genesis_block_index;
    {
        LOCK(cs_main);
        genesis_block_index = m_node.chainman->ActiveChain().Genesis();
    }
    BOOST_CHECK(coin_stats_index.LookUpStats(*genesis_block_index));

    // Check that CoinStatsIndex updates with new blocks.
    BOOST_CHECK(coin_stats_index.LookUpStats(*block_index));

    const CScript script_pub_key{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    std::vector<CMutableTransaction> noTxns;
    CreateAndProcessBlock(noTxns, script_pub_key);

    // Let the CoinStatsIndex to catch up again.
    BOOST_CHECK(coin_stats_index.BlockUntilSyncedToCurrentChain());

    const CBlockIndex* new_block_index;
    {
        LOCK(cs_main);
        new_block_index = m_node.chainman->ActiveChain().Tip();
    }
    BOOST_CHECK(coin_stats_index.LookUpStats(*new_block_index));

    BOOST_CHECK(block_index != new_block_index);

    // It is not safe to stop and destroy the index until it finishes handling
    // the last BlockConnected notification. The BlockUntilSyncedToCurrentChain()
    // call above is sufficient to ensure this, but the
    // SyncWithValidationInterfaceQueue() call below is also needed to ensure
    // TSAN always sees the test thread waiting for the notification thread, and
    // avoid potential false positive reports.
    m_node.validation_signals->SyncWithValidationInterfaceQueue();

    // Shutdown sequence (c.f. Shutdown() in init.cpp)
    coin_stats_index.Stop();
}

// Test shutdown between BlockConnected and ChainStateFlushed notifications,
// make sure index is not corrupted and is able to reload.
BOOST_FIXTURE_TEST_CASE(coinstatsindex_unclean_shutdown, TestChain100Setup)
{
    Chainstate& chainstate = Assert(m_node.chainman)->ActiveChainstate();
    const CChainParams& params = Params();
    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        BOOST_REQUIRE(index.StartBackgroundSync());
        IndexWaitSynced(index, *Assert(m_node.shutdown_signal));
        std::shared_ptr<const CBlock> new_block;
        CBlockIndex* new_block_index = nullptr;
        {
            const CScript script_pub_key{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
            const CBlock block = this->CreateBlock({}, script_pub_key, chainstate);

            new_block = std::make_shared<CBlock>(block);

            LOCK(cs_main);
            BlockValidationState state;
            BOOST_CHECK(CheckBlock(block, state, params.GetConsensus()));
            BOOST_CHECK(m_node.chainman->AcceptBlock(new_block, state, &new_block_index, true, nullptr, nullptr, true));
            CCoinsViewCache view(&chainstate.CoinsTip());
            BOOST_CHECK(chainstate.ConnectBlock(block, state, new_block_index, view));
        }
        // Send block connected notification, then stop the index without
        // sending a chainstate flushed notification. Prior to #24138, this
        // would cause the index to be corrupted and fail to reload.
        ValidationInterfaceTest::BlockConnected(ChainstateRole::NORMAL, index, new_block, new_block_index);
        index.Stop();
    }

    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        // Make sure the index can be loaded.
        BOOST_REQUIRE(index.StartBackgroundSync());
        index.Stop();
    }
}

BOOST_FIXTURE_TEST_CASE(coinstatsindex_overflow_persistence, TestChain100Setup)
{
    CBlockIndex* tip{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip())};
    const fs::path db_path{m_node.args->GetDataDirNet() / "indexes" / "coinstatsindex" / "db"};
    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        BOOST_REQUIRE(index.StartBackgroundSync());
        IndexWaitSynced(index, *Assert(m_node.shutdown_signal));
        index.Stop();
    }
    BOOST_REQUIRE(fs::exists(db_path));

    // Preserve the accounting identity while seeding counters close to INT64_MAX
    // and above 128 bits. Actual append/reorg/restart must preserve every bit.
    const arith_uint256 max{uint64_t(std::numeric_limits<CAmount>::max())};
    const arith_uint256 outputs_offset{(arith_uint256{1} << 128) + max - 1};
    const std::array<unsigned char, 5> key{'t', 0, 0, 0, 100};
    CoinStatsRecord seeded;
    {
        CDBWrapper db{DBParams{.path = db_path, .cache_bytes = 1 << 20}};
        std::pair<uint256, CoinStatsRecord> record;
        BOOST_REQUIRE(db.Read(key, record));
        BOOST_CHECK(record.first == tip->GetBlockHash());
        const arith_uint256 coinbase_offset{max - 1 - UintToArith256(record.second.coinbase)};
        const arith_uint256 prevout_offset{outputs_offset + coinbase_offset};
        record.second.prevout = ArithToUint256(UintToArith256(record.second.prevout) + prevout_offset);
        record.second.new_outputs = ArithToUint256(UintToArith256(record.second.new_outputs) + outputs_offset);
        record.second.coinbase = ArithToUint256(UintToArith256(record.second.coinbase) + coinbase_offset);
        seeded = record.second;
        DataStream serialized;
        serialized << record;
        BOOST_CHECK_EQUAL(serialized.size(), 224U);
        // Force both reload and rewind to find the predecessor in the hash index.
        BOOST_REQUIRE(db.Write(std::pair{uint8_t{'s'}, record.first}, seeded));
        record.first = uint256::ZERO;
        BOOST_REQUIRE(db.Write(key, record));
    }

    const CScript script{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    const CMutableTransaction tx{CreateValidMempoolTransaction(m_coinbase_txns[0], 0, 1, coinbaseKey, script, 49 * COIN, false)};
    uint256 block_hash, muhash;
    arith_uint256 expected_coinbase;
    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        BOOST_REQUIRE(index.StartBackgroundSync());
        IndexWaitSynced(index, *Assert(m_node.shutdown_signal));
        const auto restored{index.LookUpStats(*tip)};
        BOOST_REQUIRE(restored);
        BOOST_CHECK(restored->total_prevout_spent_amount == UintToArith256(seeded.prevout));
        BOOST_CHECK(restored->total_new_outputs_ex_coinbase_amount == UintToArith256(seeded.new_outputs));
        BOOST_CHECK(restored->total_coinbase_amount == UintToArith256(seeded.coinbase));
        BOOST_CHECK(restored->total_coinbase_amount == max - 1);

        const CBlock block{CreateAndProcessBlock({tx}, script)};
        block_hash = block.GetHash();
        BOOST_REQUIRE(index.BlockUntilSyncedToCurrentChain());
        CBlockIndex* next{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip())};
        BOOST_CHECK(next->GetBlockHash() == block_hash);
        const auto stats{index.LookUpStats(*next)};
        BOOST_REQUIRE(stats);
        expected_coinbase = UintToArith256(seeded.coinbase) + block.vtx[0]->GetValueOut();
        BOOST_CHECK(stats->total_coinbase_amount == expected_coinbase);
        BOOST_CHECK(stats->total_coinbase_amount > arith_uint256{uint64_t(std::numeric_limits<CAmount>::max())});
        BOOST_CHECK(stats->total_prevout_spent_amount == UintToArith256(seeded.prevout) + 50 * COIN);
        BOOST_CHECK(stats->total_new_outputs_ex_coinbase_amount == UintToArith256(seeded.new_outputs) + 49 * COIN);
        BOOST_CHECK_EQUAL(stats->total_subsidy, seeded.subsidy + 50 * COIN);
        BOOST_CHECK_EQUAL(stats->total_amount.value(), seeded.amount + 49 * COIN);
        BOOST_CHECK_EQUAL(stats->total_unspendables_unclaimed_rewards, seeded.unclaimed + COIN);
        const CAmount unspendable{stats->total_unspendables_genesis_block + stats->total_unspendables_bip30 +
                                 stats->total_unspendables_scripts + stats->total_unspendables_unclaimed_rewards};
        BOOST_CHECK(stats->total_prevout_spent_amount + stats->total_subsidy ==
                    stats->total_new_outputs_ex_coinbase_amount + stats->total_coinbase_amount + unspendable);
        muhash = stats->hashSerialized;

        BlockValidationState state;
        BOOST_REQUIRE(m_node.chainman->ActiveChainstate().InvalidateBlock(state, next));
        BOOST_REQUIRE(m_node.chainman->ActiveChainstate().ActivateBestChain(state));
        // A new connected block makes BaseIndex rewind before re-appending.
        CreateAndProcessBlock({tx}, CScript() << OP_TRUE);
        BOOST_REQUIRE(index.BlockUntilSyncedToCurrentChain());
        const auto reappended{index.LookUpStats(*WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()))};
        BOOST_REQUIRE(reappended);
        BOOST_CHECK(reappended->total_coinbase_amount == expected_coinbase);
        BOOST_CHECK(reappended->total_prevout_spent_amount == stats->total_prevout_spent_amount);
        BOOST_CHECK(reappended->total_new_outputs_ex_coinbase_amount == stats->total_new_outputs_ex_coinbase_amount);
        BOOST_CHECK(reappended->total_unspendables_unclaimed_rewards == stats->total_unspendables_unclaimed_rewards);
        m_node.chainman->ActiveChainstate().ForceFlushStateToDisk();
        m_node.validation_signals->SyncWithValidationInterfaceQueue();
        index.Stop();
    }
    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        BOOST_REQUIRE(index.StartBackgroundSync());
        IndexWaitSynced(index, *Assert(m_node.shutdown_signal));
        const CBlockIndex* old{WITH_LOCK(cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(block_hash))};
        const auto disconnected{index.LookUpStats(*old)};
        BOOST_REQUIRE(disconnected);
        BOOST_CHECK(disconnected->hashSerialized == muhash);
        BOOST_CHECK(disconnected->total_coinbase_amount == expected_coinbase);
        const auto current{index.LookUpStats(*WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()))};
        BOOST_REQUIRE(current);
        BOOST_CHECK(current->total_prevout_spent_amount == disconnected->total_prevout_spent_amount);
        BOOST_CHECK(current->total_new_outputs_ex_coinbase_amount == disconnected->total_new_outputs_ex_coinbase_amount);
        BOOST_CHECK(current->total_coinbase_amount == expected_coinbase);
        index.Stop();
    }
}

BOOST_FIXTURE_TEST_CASE(coinstatsindex_rpc_amount_range, TestChain100Setup)
{
    const fs::path db_path{m_node.args->GetDataDirNet() / "indexes" / "coinstatsindex" / "db"};
    {
        CoinStatsIndex index{interfaces::MakeChain(m_node), 1 << 20};
        BOOST_REQUIRE(index.Init());
        BOOST_REQUIRE(index.StartBackgroundSync());
        IndexWaitSynced(index, *Assert(m_node.shutdown_signal));
        index.Stop();
    }
    BOOST_REQUIRE(fs::exists(db_path));
    const std::array<unsigned char, 5> key{'t', 0, 0, 0, 100};
    std::pair<uint256, CoinStatsRecord> original, prev;
    {
        CDBWrapper db{DBParams{.path = db_path, .cache_bytes = 1 << 20}};
        BOOST_REQUIRE(db.Read(key, original));
        BOOST_REQUIRE(db.Read(std::array<unsigned char, 5>{'t', 0, 0, 0, 99}, prev));
    }
    JSONRPCRequest request;
    request.context = &m_node;
    request.strMethod = "gettxoutsetinfo";
    request.params = UniValue{UniValue::VARR};
    request.params.push_back("none");
    request.params.push_back(100);
    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    const arith_uint256 max{uint64_t(std::numeric_limits<CAmount>::max())};
    for (const auto member : {&CoinStatsRecord::prevout, &CoinStatsRecord::coinbase, &CoinStatsRecord::new_outputs}) {
        for (const arith_uint256& delta : std::array<arith_uint256, 4>{max, max + 1, arith_uint256{1} << 64, arith_uint256{1} << 128}) {
            auto record{original};
            record.second.*member = ArithToUint256(UintToArith256(prev.second.*member) + delta);
            {
                CDBWrapper db{DBParams{.path = db_path, .cache_bytes = 1 << 20}};
                BOOST_REQUIRE(db.Write(key, record));
            }
            auto index{std::make_unique<CoinStatsIndex>(interfaces::MakeChain(m_node), 1 << 20)};
            BOOST_REQUIRE(index->Init());
            BOOST_REQUIRE(index->StartBackgroundSync());
            IndexWaitSynced(*index, *Assert(m_node.shutdown_signal));
            BOOST_REQUIRE(!g_coin_stats_index);
            g_coin_stats_index.swap(index);
            if (delta == max) {
                BOOST_CHECK_NO_THROW({
                    const UniValue result{tableRPC.execute(request)};
                    const char* field{member == &CoinStatsRecord::prevout ? "prevout_spent" :
                                      member == &CoinStatsRecord::coinbase ? "coinbase" : "new_outputs_ex_coinbase"};
                    BOOST_CHECK_EQUAL(result.find_value("block_info").find_value(field).getValStr(), "92233720368.54775807");
                });
            } else {
                BOOST_CHECK_EXCEPTION(tableRPC.execute(request), UniValue, [](const UniValue& error) {
                    return error.find_value("code").getInt<int>() == -32603 &&
                           error.find_value("message").get_str() == "CoinStatsIndex per-block amounts exceed the RPC amount range";
                });
            }
            m_node.validation_signals->SyncWithValidationInterfaceQueue();
            g_coin_stats_index.swap(index);
            index->Stop();
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
