// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/string.h>

#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;

    // Here (and below): expected_nbits is calculated in
    // CalculateNextWorkRequired(); redoing the calculation here would be just
    // reimplementing the same code that is written in pow.cpp. Rather than
    // copy that code, we just hardcode the expected result.
    unsigned int expected_nbits = 0x1d00d86aU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    unsigned int expected_nbits = 0x1d00ffffU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    unsigned int expected_nbits = 0x1c0168fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that reducing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits-1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    unsigned int expected_nbits = 0x1d00e1fdU;
    BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, expected_nbits));
    // Test that increasing nbits further would not be a PermittedDifficultyTransition.
    unsigned int invalid_nbits = expected_nbits+1;
    BOOST_CHECK(!PermittedDifficultyTransition(chainParams->GetConsensus(), pindexLast.nHeight+1, pindexLast.nBits, invalid_nbits));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash = uint256{1};
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p2 = &blocks[m_rng.randrange(10000)];
        CBlockIndex *p3 = &blocks[m_rng.randrange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nPowTargetTimespan % consensus.nPowTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max{UintToArith256(uint256{"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"})};
        targ_max /= consensus.nPowTargetTimespan*4;
        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET4_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET4);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_CASE(asert_half_life_doubles_target)
{
    const arith_uint256 pow_limit = UintToArith256(uint256{"00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"});
    arith_uint256 ref;
    ref.SetCompact(0x1d00ffff);
    // One half-life behind schedule => target doubles (difficulty halves).
    const int64_t spacing = 600;
    const int64_t half_life = 86400;
    const int64_t height_diff = 0;
    const int64_t time_diff = half_life + spacing; // (time_diff - spacing*(height_diff+1)) / half_life = 1
    const arith_uint256 next = CalculateASERT(ref, spacing, time_diff, height_diff, pow_limit, half_life);
    BOOST_CHECK(next > ref);
    BOOST_CHECK(next <= pow_limit);
}

/** Build a short header chain covering the ASERT anchor through the block before `tip_height`. */
static std::vector<CBlockIndex> MakeAsertChain(const Consensus::Params& params, int tip_height, uint32_t nbits, int64_t extra_time = 0)
{
    const int first_height = params.nAsertAnchorHeight - 1;
    BOOST_REQUIRE(tip_height >= params.nAsertAnchorHeight);
    std::vector<CBlockIndex> blocks(tip_height - first_height + 1);
    const uint32_t t0 = 1'700'000'000;
    for (size_t i = 0; i < blocks.size(); ++i) {
        blocks[i].nHeight = first_height + static_cast<int>(i);
        blocks[i].nTime = t0 + static_cast<uint32_t>(i) * params.nPowTargetSpacing;
        blocks[i].nBits = nbits;
        if (i > 0) {
            blocks[i].pprev = &blocks[i - 1];
        }
    }
    blocks.back().nTime += extra_time;
    return blocks;
}

BOOST_AUTO_TEST_CASE(purity_asert_difficulty_is_a_floor)
{
    Consensus::Params params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    params.nPurityActivationHeight = Consensus::MAINNET_PURITY_ACTIVATION_HEIGHT;
    const int activation = params.nPurityActivationHeight;

    // On-schedule timestamps => ASERT target ≈ anchor target, leaving room both harder and easier.
    const unsigned int anchor_nbits = 0x1a00ffff;
    auto blocks = MakeAsertChain(params, activation - 1, anchor_nbits);
    const unsigned int asert_nbits = GetNextASERTWorkRequired(&blocks.back(), nullptr, params);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks.back(), nullptr, params), asert_nbits);
    BOOST_REQUIRE(DeriveTarget(asert_nbits, params.powLimit).has_value());

    arith_uint256 asert_target;
    asert_target.SetCompact(asert_nbits);

    arith_uint256 harder_target = asert_target >> 1;
    BOOST_REQUIRE(harder_target > 0);
    const unsigned int harder_nbits = harder_target.GetCompact();
    arith_uint256 harder_decoded;
    harder_decoded.SetCompact(harder_nbits);
    BOOST_REQUIRE(harder_decoded < asert_target);

    arith_uint256 easier_target = asert_target << 1;
    const arith_uint256 pow_limit = UintToArith256(params.powLimit);
    BOOST_REQUIRE(easier_target <= pow_limit);
    const unsigned int easier_nbits = easier_target.GetCompact();
    arith_uint256 easier_decoded;
    easier_decoded.SetCompact(easier_nbits);
    BOOST_REQUIRE(easier_decoded > asert_target);

    // Only the activation-height header may be harder than ASERT.
    BOOST_CHECK(CheckDifficultyBits(asert_nbits, asert_nbits, activation, params));
    BOOST_CHECK(CheckDifficultyBits(harder_nbits, asert_nbits, activation, params));
    BOOST_CHECK(!CheckDifficultyBits(easier_nbits, asert_nbits, activation, params));

    // Before and after activation, nBits must match exactly.
    BOOST_CHECK(CheckDifficultyBits(asert_nbits, asert_nbits, activation - 1, params));
    BOOST_CHECK(!CheckDifficultyBits(harder_nbits, asert_nbits, activation - 1, params));
    BOOST_CHECK(!CheckDifficultyBits(easier_nbits, asert_nbits, activation - 1, params));
    BOOST_CHECK(CheckDifficultyBits(asert_nbits, asert_nbits, activation + 1, params));
    BOOST_CHECK(!CheckDifficultyBits(harder_nbits, asert_nbits, activation + 1, params));
    BOOST_CHECK(!CheckDifficultyBits(easier_nbits, asert_nbits, activation + 1, params));

    // Illegal compact / target above powLimit remain invalid at activation.
    BOOST_CHECK(!CheckDifficultyBits(/* overflow */ ~0x00800000U, asert_nbits, activation, params));
    BOOST_CHECK(!CheckDifficultyBits(UintToArith256(params.powLimit).GetCompact(/* fNegative */ true), asert_nbits, activation, params));
    arith_uint256 over_limit = pow_limit << 1;
    BOOST_CHECK(!CheckDifficultyBits(over_limit.GetCompact(), asert_nbits, activation, params));

    // CheckProofOfWork still uses the claimed nBits: a hash that only meets ASERT
    // must fail if the header declared a harder target.
    BOOST_CHECK(CheckProofOfWork(ArithToUint256(asert_target), asert_nbits, params));
    BOOST_CHECK(!CheckProofOfWork(ArithToUint256(asert_target), harder_nbits, params));
}

BOOST_AUTO_TEST_CASE(purity_asert_accepts_legacy_daa_high_difficulty)
{
    Consensus::Params params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    params.nPurityActivationHeight = Consensus::MAINNET_PURITY_ACTIVATION_HEIGHT;
    const int activation = params.nPurityActivationHeight;

    // Current-mainnet-like compact, well below powLimit.
    const unsigned int legacy_nbits = 0x17078e04;
    // ~30 days behind schedule eases ASERT well above the legacy target.
    auto blocks = MakeAsertChain(params, activation - 1, legacy_nbits, /* extra_time */ 30 * 24 * 60 * 60);
    const unsigned int asert_nbits = GetNextASERTWorkRequired(&blocks.back(), nullptr, params);

    const auto legacy_target = DeriveTarget(legacy_nbits, params.powLimit);
    const auto asert_target = DeriveTarget(asert_nbits, params.powLimit);
    BOOST_REQUIRE(legacy_target.has_value() && asert_target.has_value());
    BOOST_REQUIRE(*legacy_target < *asert_target);

    BOOST_CHECK(CheckDifficultyBits(legacy_nbits, asert_nbits, activation, params));
    BOOST_CHECK(CheckDifficultyBits(asert_nbits, asert_nbits, activation, params));
    BOOST_CHECK(!CheckDifficultyBits(legacy_nbits, asert_nbits, activation + 1, params));
}

BOOST_AUTO_TEST_CASE(asert_at_purity_activation_uses_anchor)
{
    Consensus::Params params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    auto blocks = MakeAsertChain(params, Consensus::MAINNET_PURITY_ACTIVATION_HEIGHT - 1, 0x1a00ffff);
    const unsigned int asert_nbits = GetNextASERTWorkRequired(&blocks.back(), nullptr, params);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks.back(), nullptr, params), asert_nbits);
}

BOOST_AUTO_TEST_CASE(purity_regtest_retains_no_retargeting)
{
    auto params = CreateChainParams(*m_node.args, ChainType::REGTEST)->GetConsensus();
    params.nPurityActivationHeight = 200;
    CBlockIndex previous;
    previous.nHeight = 199;
    previous.nBits = 0x207fffff;
    CBlockHeader next;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&previous, &next, params), previous.nBits);
}

BOOST_AUTO_TEST_CASE(signet_asert_activation_uses_wide_limit)
{
    auto params = CreateChainParams(*m_node.args, ChainType::SIGNET)->GetConsensus();
    params.nPurityActivationHeight = 11;
    params.nAsertAnchorHeight = 6;
    const auto limit = UintToArith256(params.powLimit);
    BOOST_REQUIRE_EQUAL(limit.bits(), 234U);
    auto blocks = MakeAsertChain(params, 10, 0x1e0377ae);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks.back(), nullptr, params), 0x1e0377aeU);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks[blocks.size() - 2], nullptr, params), 0x1e0377aeU);
}

BOOST_AUTO_TEST_CASE(signet_selected_height_anchor_work)
{
    auto params = CChainParams::SigNet({})->GetConsensus();
    params.nPurityActivationHeight = 318621;
    params.nAsertAnchorHeight = 318616;
    std::vector<CBlockIndex> headers(6);
    const uint32_t times[]{1787261860, 1787263660, 1787265035, 1787266074, 1787266163, 1787266302};
    for (size_t i = 0; i < headers.size(); ++i) {
        headers[i].nHeight = 318615 + i;
        headers[i].nTime = times[i];
        headers[i].nBits = 0x1d13c0c2;
        headers[i].pprev = i ? &headers[i - 1] : nullptr;
    }
    const auto bits = GetNextWorkRequired(&headers.back(), nullptr, params);
    BOOST_CHECK_EQUAL(bits, 0x1d13fbb5U);
    BOOST_CHECK(CheckDifficultyBits(0x1d13c0c2, bits, 318621, params));
    BOOST_CHECK(!CheckDifficultyBits(0x1d13c0c2, bits, 318622, params));
}

BOOST_AUTO_TEST_CASE(signet_asert_integer_reference)
{
    const arith_uint256 limit{UintToArith256(uint256{"00000377ae000000000000000000000000000000000000000000000000000000"})};
    const arith_uint256 ref{UintToArith256(uint256{"0000010000000000000000000000000000000000000000000000000000000000"})};
    // Generated with unbounded integers and signed division toward zero.
    const struct { int64_t extra_time; const char* target; } vectors[] = {
        {-30000000, "0000000000000000000000000000000000000000000000000000000000000001"},
        {-86401, "0000008000000000000000000000000000000000000000000000000000000000"},
        {-86400, "0000008000000000000000000000000000000000000000000000000000000000"},
        {-12345, "000000e7e3800000000000000000000000000000000000000000000000000000"},
        {-1, "0000010000000000000000000000000000000000000000000000000000000000"},
        {0, "0000010000000000000000000000000000000000000000000000000000000000"},
        {1, "0000010000000000000000000000000000000000000000000000000000000000"},
        {12345, "0000011aae000000000000000000000000000000000000000000000000000000"},
        {43199, "0000016a01000000000000000000000000000000000000000000000000000000"},
        {86399, "000001ffff000000000000000000000000000000000000000000000000000000"},
        {86400, "0000020000000000000000000000000000000000000000000000000000000000"},
        {86401, "0000020000000000000000000000000000000000000000000000000000000000"},
        {30000000, "00000377ae000000000000000000000000000000000000000000000000000000"},
    };
    for (const auto& v : vectors) {
        BOOST_CHECK_EQUAL(CalculateSignetASERT(ref, 600, 3000 + v.extra_time, 4, limit, 86400),
                          UintToArith256(*uint256::FromHex(v.target)));
    }
}

BOOST_AUTO_TEST_CASE(signet_asert_matches_original_on_mainnet_domain)
{
    const auto params = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    const auto limit = UintToArith256(params.powLimit);
    for (const auto& ref : {arith_uint256{1}, arith_uint256{limit >> 1}, limit}) {
        for (int64_t height : {0, 4, 2016}) {
            for (int64_t extra : {-30000000, -86401, -12345, 0, 12345, 86401, 30000000}) {
                const int64_t time = 600 * (height + 1) + extra;
                BOOST_CHECK_EQUAL(CalculateSignetASERT(ref, 600, time, height, limit, 86400),
                                  CalculateASERT(ref, 600, time, height, limit, 86400));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(signet_asert_239_bit_multiply_and_shift_boundaries)
{
    const arith_uint256 limit = (~arith_uint256{}) >> 17;
    BOOST_REQUIRE_EQUAL(limit.bits(), 239U);
    const auto ref = limit >> 1;
    // exponent's fractional part is 65535: the largest polynomial factor.
    BOOST_CHECK_EQUAL(CalculateSignetASERT(ref, 600, 86999, 0, limit, 86400), (ref * 131071U) >> 16);
    BOOST_CHECK_EQUAL(CalculateSignetASERT(limit, 600, 30000000, 0, limit, 86400), limit);
    BOOST_CHECK_EQUAL(CalculateSignetASERT(arith_uint256{1}, 600, -30000000, 0, limit, 86400), arith_uint256{1});
}

BOOST_AUTO_TEST_SUITE_END()
