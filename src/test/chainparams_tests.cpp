// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <deploymentstatus.h>
#include <versionbits.h>

#include <limits>
#include <string>
#include <vector>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainparams_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(mainnet_assumevalid_default)
{
    const auto params = CreateChainParams(m_args, ChainType::MAIN);
    BOOST_CHECK_EQUAL(params->GetConsensus().defaultAssumeValid,
                      uint256{"00000000000000000000807f9dc917442a67910426d79ebb2f8aa2149327ce8a"});
}

BOOST_AUTO_TEST_CASE(purity_activation_height_mainnet)
{
    ArgsManager args;
    args.ForceSetArg("-datadir", fs::PathToString(m_args.GetDataDirBase()));
    const auto chainParams = CreateChainParams(args, ChainType::MAIN);
    BOOST_CHECK_EQUAL(chainParams->GetConsensus().nPurityActivationHeight, Consensus::MAINNET_PURITY_ACTIVATION_HEIGHT);
    BOOST_CHECK_EQUAL(chainParams->GetConsensus().nAsertAnchorHeight, Consensus::MAINNET_ASERT_ANCHOR_HEIGHT);
}

BOOST_AUTO_TEST_CASE(purity_activation_block_hash_pinned)
{
    ArgsManager args;
    args.ForceSetArg("-datadir", fs::PathToString(m_args.GetDataDirBase()));
    const auto chainParams = CreateChainParams(args, ChainType::MAIN);
    const auto& consensus = chainParams->GetConsensus();

    const uint256 expected{"0000000000000000003ea74f4dafdda7ed4e02c4c1ccb9768e0ca4f9e1a35159"};
    BOOST_CHECK_EQUAL(consensus.hashPurityActivationBlock, expected);

    // The pin is a consensus rule evaluated from Consensus::Params alone; it
    // does not consult the (disableable) -checkpoints machinery.
    const int activation = consensus.nPurityActivationHeight;
    BOOST_CHECK(Consensus::PurityActivationBlockPermitted(activation, expected, consensus));

    // A real conflicting mainnet block at the same height must be rejected.
    const uint256 conflicting{"0000000000000000000121f7aa4329b9d040bde9eac2d49b5219e57742ccbc9d"};
    BOOST_CHECK(!Consensus::PurityActivationBlockPermitted(activation, conflicting, consensus));
    BOOST_CHECK(!Consensus::PurityActivationBlockPermitted(activation, uint256{}, consensus));

    // Other heights are unconstrained by the pin.
    BOOST_CHECK(Consensus::PurityActivationBlockPermitted(activation - 1, conflicting, consensus));
    BOOST_CHECK(Consensus::PurityActivationBlockPermitted(activation + 1, conflicting, consensus));
}

BOOST_AUTO_TEST_CASE(purity_activation_block_hash_unset_off_mainnet)
{
    ArgsManager args;
    args.ForceSetArg("-datadir", fs::PathToString(m_args.GetDataDirBase()));
    const auto chainParams = CreateChainParams(args, ChainType::REGTEST);
    const auto& consensus = chainParams->GetConsensus();
    BOOST_CHECK(consensus.hashPurityActivationBlock.IsNull());
    // Unset pin never constrains any block.
    BOOST_CHECK(Consensus::PurityActivationBlockPermitted(0, uint256{}, consensus));
    BOOST_CHECK(Consensus::PurityActivationBlockPermitted(Consensus::MAINNET_PURITY_ACTIVATION_HEIGHT, uint256{}, consensus));
}

BOOST_AUTO_TEST_CASE(purity_activation_height_ignored_off_mainnet)
{
    ArgsManager args;
    args.ForceSetArg("-datadir", fs::PathToString(m_args.GetDataDirBase()));
    const auto chainParams = CreateChainParams(args, ChainType::REGTEST);
    BOOST_CHECK_EQUAL(chainParams->GetConsensus().nPurityActivationHeight, std::numeric_limits<int>::max());
}

BOOST_AUTO_TEST_CASE(purity_activation_height_enables_rdts)
{
    ArgsManager args;
    args.ForceSetArg("-datadir", fs::PathToString(m_args.GetDataDirBase()));
    const auto chainParams = CreateChainParams(args, ChainType::MAIN);
    const auto& consensus = chainParams->GetConsensus();
    VersionBitsCache versionbitscache;

    // Height 0 is below activation; BIP9 path is not ACTIVE for genesis.
    BOOST_CHECK(!DeploymentActiveAfter(nullptr, consensus, Consensus::DEPLOYMENT_REDUCED_DATA, versionbitscache));

    CBlockIndex tip;
    tip.nHeight = Consensus::MAINNET_PURITY_ACTIVATION_HEIGHT - 1; // next block is the Purity activation height
    tip.pprev = nullptr;
    // Permanent RDTS activates via Purity height before BIP9 is consulted.
    BOOST_CHECK(DeploymentActiveAfter(&tip, consensus, Consensus::DEPLOYMENT_REDUCED_DATA, versionbitscache));
}

BOOST_AUTO_TEST_CASE(reduced_data_grandfather_height)
{
    auto params = CreateChainParams(m_args, ChainType::REGTEST)->GetConsensus();
    params.nMinerConfirmationWindow = 10;
    params.nRuleChangeActivationThreshold = 8;
    auto& deployment = params.vDeployments[Consensus::DEPLOYMENT_REDUCED_DATA];
    deployment.nStartTime = 0;
    deployment.nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
    params.nPurityActivationHeight = 15;
    params.nReducedDataGrandfatherHeight = 15;
    params.nReducedDataGrandfatherFixHeight = 15;
    VersionBitsCache cache;
    std::vector<CBlockIndex> blocks(60);
    for (int height = 0; height < 60; ++height) {
        auto& block = blocks[height];
        block.nHeight = height;
        block.nTime = 1000 + height;
        block.nVersion = VERSIONBITS_TOP_BITS | (height >= 20 ? (1 << deployment.bit) : 0);
        block.pprev = height ? &blocks[height - 1] : nullptr;
        block.BuildSkip();
    }
    BOOST_CHECK(cache.State(&blocks[19], params, Consensus::DEPLOYMENT_REDUCED_DATA) == ThresholdState::STARTED);
    BOOST_CHECK(cache.State(&blocks[29], params, Consensus::DEPLOYMENT_REDUCED_DATA) == ThresholdState::LOCKED_IN);
    BOOST_CHECK(cache.State(&blocks[39], params, Consensus::DEPLOYMENT_REDUCED_DATA) == ThresholdState::ACTIVE);
    for (int height : {14, 15, 16, 20, 30, 40, 59}) {
        BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[height - 1], params, cache), height < 15 ? 0 : 15);
    }
    const int boundary = GetReducedDataGrandfatherHeight(&blocks[39], params, cache);
    BOOST_CHECK(14 < boundary);
    BOOST_CHECK(!(15 < boundary));
    BOOST_CHECK(!(16 < boundary));

    // No correction activation means deployed historical rules stay in force.
    params.nReducedDataGrandfatherFixHeight = std::numeric_limits<int>::max();
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[19], params, cache), 10);
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[29], params, cache), 30);
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[39], params, cache), 40);
    params.nReducedDataGrandfatherFixHeight = 45;
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[43], params, cache), 40);
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[44], params, cache), 15);

    // Non-Purity BIP9 deployments retain their active/inactive and expiry rules.
    params.nPurityActivationHeight = std::numeric_limits<int>::max();
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[19], params, cache), 0);
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[29], params, cache), 0);
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[39], params, cache), 40);
    deployment.active_duration = 10;
    BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&blocks[49], params, cache), 0);
}

BOOST_AUTO_TEST_CASE(reduced_data_mainnet_correction_at_purity_activation)
{
    const auto params = CreateChainParams(m_args, ChainType::MAIN);
    const auto& consensus = params->GetConsensus();
    BOOST_CHECK_EQUAL(consensus.nReducedDataGrandfatherHeight, 961637);
    BOOST_REQUIRE_EQUAL(consensus.nReducedDataGrandfatherFixHeight, 961637);
    VersionBitsCache cache;
    CBlockIndex previous;
    for (int height : {961637, 961638, 965664, 967297, 970000}) {
        previous.nHeight = height - 1;
        BOOST_CHECK_EQUAL(GetReducedDataGrandfatherHeight(&previous, consensus, cache), 961637);
    }
}

BOOST_AUTO_TEST_SUITE_END()
