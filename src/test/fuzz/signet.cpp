// Copyright (c) 2020-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <signet.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <cstdint>
#include <optional>
#include <vector>

void initialize_signet()
{
    static const auto testing_setup = MakeNoLogFileContext<>(ChainType::SIGNET);
}

FUZZ_TARGET(signet, .init = initialize_signet)
{
    FuzzedDataProvider fuzzed_data_provider{buffer.data(), buffer.size()};
    const std::optional<CBlock> block = ConsumeDeserializable<CBlock>(fuzzed_data_provider, TX_WITH_WITNESS);
    if (!block) {
        return;
    }
    (void)CheckSignetBlockSolution(*block, Params().GetConsensus(), fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 1000000));
    auto postfork = Params().GetConsensus();
    postfork.nPurityActivationHeight = 1;
    postfork.signet_challenge_postfork = ParseHex(
        "522102fe8d1eb1bcb3432b1db5833ff5f2226d9cb5e65cee430558c18ed3a3c86ce1af"
        "2103d528ecd9b696b54c907a9ed045447a79bb408ec39b68df504bb51f459bc3ffc9"
        "21025d045857332d5b9e541514731622af8d60c180165d971a61e06b70a9b383476553ae");
    (void)CheckSignetBlockSolution(*block, postfork, 1);
    (void)SignetTxs::Create(*block, ConsumeScript(fuzzed_data_provider));
}
