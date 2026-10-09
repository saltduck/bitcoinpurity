// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <index/coinstatsindex.h>
#include <kernel/coinstats.h>
#include <node/blockstorage.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <txdb.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <thread>

namespace {
class FailingCursor final : public CCoinsViewCursor
{
    std::unique_ptr<CCoinsViewCursor> m_cursor;
    bool m_fail_key;

public:
    FailingCursor(std::unique_ptr<CCoinsViewCursor> cursor, bool fail_key)
        : CCoinsViewCursor(cursor->GetBestBlock()), m_cursor(std::move(cursor)), m_fail_key(fail_key) {}
    bool GetKey(COutPoint& key) const override { return !m_fail_key && m_cursor->GetKey(key); }
    bool GetValue(Coin&) const override { return false; }
    bool Valid() const override { return m_cursor->Valid(); }
    void Next() override { m_cursor->Next(); }
};

class CursorTestView final : public CCoinsViewBacked
{
public:
    mutable int cursor_calls{0};
    mutable int best_block_calls{0};
    std::function<void()> before_cursor;
    std::optional<bool> fail_key;

    explicit CursorTestView(CCoinsView* view) : CCoinsViewBacked(view) {}
    uint256 GetBestBlock() const override
    {
        ++best_block_calls;
        return base->GetBestBlock();
    }
    std::unique_ptr<CCoinsViewCursor> Cursor() const override
    {
        ++cursor_calls;
        if (before_cursor) before_cursor();
        auto cursor{base->Cursor()};
        if (fail_key) return std::make_unique<FailingCursor>(std::move(cursor), *fail_key);
        return cursor;
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(coinstats_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(cursor_snapshot)
{
    using kernel::CoinStatsHashType;
    auto& chainstate{m_node.chainman->ActiveChainstate()};
    chainstate.ForceFlushStateToDisk();
    CursorTestView view{&WITH_LOCK(cs_main, return chainstate.CoinsDB())};
    const CScript script{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    for (const auto mode : {CoinStatsHashType::HASH_SERIALIZED, CoinStatsHashType::MUHASH, CoinStatsHashType::NONE}) {
        view.cursor_calls = view.best_block_calls = 0;
        // Advance and flush exactly at cursor acquisition, after any separate
        // best-block read. No production scheduling hook or delay is needed.
        view.before_cursor = [&] {
            bool available{false};
            std::thread contender{[&] {
                TRY_LOCK(cs_main, lock);
                available = bool(lock);
            }};
            contender.join();
            BOOST_CHECK(!available);
            CreateAndProcessBlock({}, script);
            chainstate.ForceFlushStateToDisk();
        };
        int interruptions{0};
        const auto stats{kernel::ComputeUTXOStats(mode, &view, m_node.chainman->m_blockman, [&] {
            AssertLockNotHeld(cs_main);
            ++interruptions;
        })};
        BOOST_REQUIRE(stats);
        const auto* tip{WITH_LOCK(cs_main, return chainstate.m_chain.Tip())};
        BOOST_CHECK_EQUAL(stats->nHeight, tip->nHeight);
        BOOST_CHECK(stats->hashBlock == tip->GetBlockHash());
        BOOST_CHECK_EQUAL(stats->coins_count, tip->nHeight);
        BOOST_CHECK_EQUAL(interruptions, stats->coins_count);
        BOOST_CHECK_EQUAL(view.cursor_calls, 1);
        BOOST_CHECK_EQUAL(view.best_block_calls, 0);
        view.before_cursor = {};
        const auto stable{WITH_LOCK(cs_main, return kernel::ComputeUTXOStats(mode, &view, m_node.chainman->m_blockman))};
        BOOST_REQUIRE(stable);
        BOOST_CHECK(stable->hashSerialized == stats->hashSerialized);
        BOOST_CHECK(stable->total_amount == stats->total_amount);
        BOOST_CHECK_EQUAL(stable->nTransactionOutputs, stats->nTransactionOutputs);
        BOOST_CHECK_EQUAL(stable->nBogoSize, stats->nBogoSize);
        BOOST_CHECK_EQUAL(stats->nDiskSize, view.EstimateSize());
    }
    BOOST_CHECK(!g_coin_stats_index);
}

BOOST_AUTO_TEST_CASE(cursor_failure_and_interruption)
{
    auto& chainstate{m_node.chainman->ActiveChainstate()};
    chainstate.ForceFlushStateToDisk();
    CursorTestView view{&WITH_LOCK(cs_main, return chainstate.CoinsDB())};
    for (const auto mode : {kernel::CoinStatsHashType::HASH_SERIALIZED, kernel::CoinStatsHashType::MUHASH, kernel::CoinStatsHashType::NONE}) {
        for (bool fail_key : {false, true}) {
            view.fail_key = fail_key;
            BOOST_CHECK(!kernel::ComputeUTXOStats(mode, &view, m_node.chainman->m_blockman));
        }
        view.fail_key.reset();
        BOOST_CHECK_THROW(kernel::ComputeUTXOStats(mode, &view, m_node.chainman->m_blockman, [] {
            throw std::runtime_error{"interrupted"};
        }), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(scan_snapshot_survives_flush)
{
    auto& chainstate{m_node.chainman->ActiveChainstate()};
    chainstate.ForceFlushStateToDisk();
    auto& view{WITH_LOCK(cs_main, return chainstate.CoinsDB())};
    const CScript script{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    for (const auto mode : {kernel::CoinStatsHashType::HASH_SERIALIZED, kernel::CoinStatsHashType::MUHASH, kernel::CoinStatsHashType::NONE}) {
        const auto before{kernel::ComputeUTXOStats(mode, &view, m_node.chainman->m_blockman)};
        BOOST_REQUIRE(before);
        bool advanced{false};
        const auto during{kernel::ComputeUTXOStats(mode, &view, m_node.chainman->m_blockman, [&] {
            AssertLockNotHeld(cs_main);
            if (!advanced) {
                advanced = true;
                CreateAndProcessBlock({}, script);
                chainstate.ForceFlushStateToDisk();
            }
        })};
        BOOST_REQUIRE(during);
        BOOST_CHECK(advanced);
        BOOST_CHECK_EQUAL(during->nHeight, before->nHeight);
        BOOST_CHECK(during->hashBlock == before->hashBlock);
        BOOST_CHECK(during->hashSerialized == before->hashSerialized);
        BOOST_CHECK(during->total_amount == before->total_amount);
        BOOST_CHECK_EQUAL(during->coins_count, before->coins_count);
    }
}

BOOST_AUTO_TEST_SUITE_END()
