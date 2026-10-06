// Copyright (c) 2018-2020 The Bitcoin developers
// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Adapted from src/test/blockstatus_tests.cpp in Bitcoin ABC
// 335857dbf6fc706910c1e888ae3afd3cecf859e0 and Bitcoin Cash Node
// 07576013c91ff4a3a74acd85f189c69121cdad1b. Original parked-bit coverage:
// 7878c6b91cacdea6fe4b3de95414bbb8133c5b3a; independent mask clearing:
// 0e5d128ef3e0d584c54a9ebc2d409f2737e07d87.

#include <chain.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <array>
#include <boost/test/unit_test.hpp>

namespace {
constexpr std::array<BlockStatus, 6> VALIDITIES{
    BLOCK_VALID_UNKNOWN, BLOCK_VALID_RESERVED, BLOCK_VALID_TREE,
    BLOCK_VALID_TRANSACTIONS, BLOCK_VALID_CHAIN, BLOCK_VALID_SCRIPTS};
constexpr std::array<BlockStatus, 6> FLAGS{
    BLOCK_HAVE_DATA, BLOCK_HAVE_UNDO, BLOCK_FAILED_VALID,
    BLOCK_FAILED_CHILD, BLOCK_PARKED, BLOCK_PARKED_CHILD};

void CheckBlockStatus(uint32_t status, BlockStatus validity, const std::array<bool, 6>& flags) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    CBlockIndex index;
    index.nStatus = status;
    BOOST_CHECK_EQUAL(status & BLOCK_VALID_MASK, validity);
    for (size_t i = 0; i < FLAGS.size(); ++i) {
        BOOST_CHECK_EQUAL(bool(status & FLAGS[i]), flags[i]);
    }
    BOOST_CHECK_EQUAL(bool(status & BLOCK_FAILED_MASK), flags[2] || flags[3]);
    BOOST_CHECK_EQUAL(bool(status & BLOCK_PARKED_MASK), flags[4] || flags[5]);
    for (BlockStatus requested : VALIDITIES) {
        BOOST_CHECK_EQUAL(index.IsValid(requested), !flags[2] && !flags[3] && validity >= requested);
    }
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(blockstatus_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(status_flag_composition)
{
    LOCK(cs_main);
    CheckBlockStatus(CBlockIndex{}.nStatus, BLOCK_VALID_UNKNOWN, {});
    for (uint32_t permutation = 0; permutation < 64; ++permutation) {
        std::array<bool, 6> expected{};
        uint32_t bits{0};
        for (size_t i = 0; i < FLAGS.size(); ++i) {
            expected[i] = permutation & (1U << i);
            if (expected[i]) bits |= FLAGS[i];
        }
        for (BlockStatus validity : VALIDITIES) {
            const uint32_t status = bits | validity;
            CheckBlockStatus(status, validity, expected);
            for (size_t i = 0; i < FLAGS.size(); ++i) {
                for (bool enabled : {false, true}) {
                    auto changed = expected;
                    changed[i] = enabled;
                    CheckBlockStatus((status & ~FLAGS[i]) | (enabled ? FLAGS[i] : 0U), validity, changed);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(clear_masks_independently)
{
    LOCK(cs_main);
    for (uint32_t permutation = 0; permutation < 64; ++permutation) {
        std::array<bool, 6> expected{};
        uint32_t bits{0};
        for (size_t i = 0; i < FLAGS.size(); ++i) {
            expected[i] = permutation & (1U << i);
            if (expected[i]) bits |= FLAGS[i];
        }
        for (BlockStatus validity : VALIDITIES) {
            auto without_failure = expected;
            without_failure[2] = without_failure[3] = false;
            CheckBlockStatus((bits | validity) & ~BLOCK_FAILED_MASK, validity, without_failure);
            auto without_parking = expected;
            without_parking[4] = without_parking[5] = false;
            CheckBlockStatus((bits | validity) & ~BLOCK_PARKED_MASK, validity, without_parking);
        }
    }
}

BOOST_AUTO_TEST_CASE(validity_changes_preserve_parking)
{
    LOCK(cs_main);
    for (uint32_t permutation = 0; permutation < 64; ++permutation) {
        uint32_t bits{0};
        for (size_t i = 0; i < FLAGS.size(); ++i) {
            if (permutation & (1U << i)) bits |= FLAGS[i];
        }
        for (BlockStatus old_validity : VALIDITIES) {
            for (BlockStatus new_validity : VALIDITIES) {
                CBlockIndex index;
                index.nStatus = bits | old_validity;
                const bool should_raise = !(bits & BLOCK_FAILED_MASK) && new_validity > old_validity;
                BOOST_CHECK_EQUAL(index.RaiseValidity(new_validity), should_raise);
                BOOST_CHECK_EQUAL(index.nStatus & ~BLOCK_VALID_MASK, bits);
                BOOST_CHECK_EQUAL(index.nStatus & BLOCK_VALID_MASK, should_raise ? new_validity : old_validity);
                // Upstream's withValidity permits replacing the underlying level
                // even when flags are present; Purity represents that as raw bits.
                const uint32_t replaced = (index.nStatus & ~BLOCK_VALID_MASK) | new_validity;
                BOOST_CHECK_EQUAL(replaced & ~BLOCK_VALID_MASK, bits);
                BOOST_CHECK_EQUAL(replaced & BLOCK_VALID_MASK, new_validity);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
