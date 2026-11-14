#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <headerssync.h>
#include <pow.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/time.h>
#include <validation.h>

#include <deque>
#include <iterator>
#include <vector>

static void initialize_headers_sync_state_fuzz()
{
    static const auto testing_setup = MakeNoLogFileContext<>(
        /*chain_type=*/ChainType::MAIN);
}

void MakeHeadersContinuous(
    const CBlockHeader& genesis_header,
    const std::vector<CBlockHeader>& all_headers,
    std::vector<CBlockHeader>& new_headers)
{
    Assume(!new_headers.empty());

    const CBlockHeader* prev_header{
        all_headers.empty() ? &genesis_header : &all_headers.back()};

    for (auto& header : new_headers) {
        header.hashPrevBlock = prev_header->GetHash();

        prev_header = &header;
    }
}

class FuzzedHeadersSyncState : public HeadersSyncState
{
public:
    FuzzedHeadersSyncState(const unsigned commit_offset, const CBlockIndex* chain_start, const arith_uint256& minimum_required_work)
        : HeadersSyncState(/*id=*/0, Params().GetConsensus(), chain_start, minimum_required_work)
    {
        const_cast<unsigned&>(m_commit_offset) = commit_offset;
    }
};

FUZZ_TARGET(headers_sync_state, .init = initialize_headers_sync_state_fuzz)
{
    SeedRandomStateForTest(SeedRand::ZEROS);
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    auto mock_time{ConsumeTime(fuzzed_data_provider)};

    CBlockHeader genesis_header{Params().GenesisBlock()};
    CBlockIndex start_index(genesis_header);

    if (mock_time < start_index.GetMedianTimePast()) return;
    SetMockTime(mock_time);

    const uint256 genesis_hash = genesis_header.GetHash();
    start_index.phashBlock = &genesis_hash;

    arith_uint256 min_work{UintToArith256(ConsumeUInt256(fuzzed_data_provider))};
    FuzzedHeadersSyncState headers_sync(
        /*commit_offset=*/fuzzed_data_provider.ConsumeIntegralInRange<unsigned>(1, 1024),
        /*chain_start=*/&start_index,
        /*minimum_required_work=*/min_work);

    // Store headers for potential redownload phase.
    std::vector<CBlockHeader> all_headers;
    std::vector<CBlockHeader>::const_iterator redownloaded_it;
    bool presync{true};
    bool requested_more{true};

    while (requested_more) {
        std::vector<CBlockHeader> headers;

        // Consume headers from fuzzer or maybe replay headers if we got to the
        // redownload phase.
        if (presync || fuzzed_data_provider.ConsumeBool()) {
            auto deser_headers = ConsumeDeserializable<std::vector<CBlockHeader>>(fuzzed_data_provider);
            if (!deser_headers || deser_headers->empty()) return;

            if (fuzzed_data_provider.ConsumeBool()) {
                MakeHeadersContinuous(genesis_header, all_headers, *deser_headers);
            }

            headers.swap(*deser_headers);
        } else if (auto num_headers_left{std::distance(redownloaded_it, all_headers.cend())}; num_headers_left > 0) {
            // Consume some headers from the redownload buffer (At least one
            // header is consumed).
            auto begin_it{redownloaded_it};
            std::advance(redownloaded_it, fuzzed_data_provider.ConsumeIntegralInRange<int>(1, num_headers_left));
            headers.insert(headers.cend(), begin_it, redownloaded_it);
        }

        if (headers.empty()) return;
        auto result = headers_sync.ProcessNextHeaders(headers, fuzzed_data_provider.ConsumeBool());
        requested_more = result.request_more;

        if (result.request_more) {
            if (presync) {
                all_headers.insert(all_headers.cend(), headers.cbegin(), headers.cend());

                if (headers_sync.GetState() == HeadersSyncState::State::REDOWNLOAD) {
                    presync = false;
                    redownloaded_it = all_headers.cbegin();

                    // If we get to redownloading, the presynced headers need
                    // to have the min amount of work on them.
                    assert(CalculateClaimedHeadersWork(all_headers) >= min_work);
                }
            }

            (void)headers_sync.NextHeadersRequestLocator();
        }
    }
}

namespace {
/** Block index entries kept at stable addresses, linked by order like presync.
 * No skip pointers: the index starts above genesis. */
class OrderedIndex
{
    std::deque<uint256> m_hashes;
    std::deque<CBlockIndex> m_index;

public:
    const CBlockIndex& Append(const CBlockHeader& header, const CBlockIndex* prev, const Consensus::Params& params)
    {
        m_hashes.push_back(header.GetHash());
        CBlockIndex& entry{m_index.emplace_back(header)};
        entry.phashBlock = &m_hashes.back();
        entry.pprev = const_cast<CBlockIndex*>(prev);
        entry.nHeight = prev ? prev->nHeight + 1 : params.nAsertAnchorHeight - 2;
        entry.nChainWork = (prev ? prev->nChainWork : arith_uint256{}) + GetBlockProof(entry);
        return entry;
    }
};

uint32_t RequiredBits(const CBlockIndex& prev, const CBlockHeader& header, const Consensus::Params& params)
{
    // Pre-activation heights here never reach a retarget with enough history,
    // and the anchor retarget accepts unchanged bits.
    if (prev.nHeight + 1 < params.nPurityActivationHeight) return prev.nBits;
    return GetNextWorkRequired(&prev, &header, params);
}

/** The difficulty and time rules full header validation applies. */
void AssertValidDifficulty(const CBlockIndex& prev, const CBlockHeader& header, const Consensus::Params& params)
{
    const int height{prev.nHeight + 1};
    assert(header.GetBlockTime() <= GetTime() + MAX_FUTURE_BLOCK_TIME);
    if (height < params.nPurityActivationHeight) {
        assert(PermittedDifficultyTransition(params, height, prev.nBits, header.nBits));
    } else {
        assert(CheckDifficultyBits(header.nBits, GetNextWorkRequired(&prev, &header, params), height, params));
    }
}
} // namespace

// Starts near the mainnet ASERT anchor so headers reach Purity activation, and
// checks that every header presync or redownload accepts has the nBits and
// time that full validation requires.
FUZZ_TARGET(headers_sync_state_asert, .init = initialize_headers_sync_state_fuzz)
{
    SeedRandomStateForTest(SeedRand::ZEROS);
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const Consensus::Params& params{Params().GetConsensus()};
    const arith_uint256 pow_limit{UintToArith256(params.powLimit)};

    OrderedIndex index;
    CBlockHeader first;
    first.nVersion = 0x20000000;
    first.nTime = fuzzed_data_provider.ConsumeIntegralInRange<uint32_t>(1'700'000'000, 1'800'000'000);
    first.nBits = arith_uint256{pow_limit >> fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 160)}.GetCompact();
    const CBlockIndex* chain_start{&index.Append(first, nullptr, params)};

    // Start below the anchor, between anchor and activation, or after activation.
    const int extra{fuzzed_data_provider.ConsumeIntegralInRange<int>(0, params.nPurityActivationHeight - chain_start->nHeight + 8)};
    for (int i = 0; i < extra; ++i) {
        CBlockHeader next;
        next.nVersion = 0x20000000;
        next.hashPrevBlock = chain_start->GetBlockHash();
        next.nTime = chain_start->nTime + fuzzed_data_provider.ConsumeIntegralInRange<uint32_t>(0, 3 * 60 * 60);
        next.nBits = RequiredBits(*chain_start, next, params);
        chain_start = &index.Append(next, chain_start, params);
    }

    const auto mock_time{ConsumeTime(fuzzed_data_provider)};
    if (mock_time < chain_start->GetMedianTimePast()) return;
    SetMockTime(mock_time);

    const arith_uint256 min_work{chain_start->nChainWork + UintToArith256(ConsumeUInt256(fuzzed_data_provider)) / (arith_uint256{1} << fuzzed_data_provider.ConsumeIntegralInRange<int>(0, 255))};
    HeadersSyncState headers_sync{/*id=*/0, params, chain_start, min_work};

    std::vector<CBlockHeader> all_headers;
    std::vector<CBlockHeader>::const_iterator redownloaded_it;
    const CBlockIndex* presync_tip{chain_start};
    const CBlockIndex* redownload_tip{chain_start};
    bool presync{true};
    bool requested_more{true};

    while (requested_more) {
        std::vector<CBlockHeader> headers;
        if (presync || fuzzed_data_provider.ConsumeBool()) {
            auto deser_headers{ConsumeDeserializable<std::vector<CBlockHeader>>(fuzzed_data_provider)};
            if (!deser_headers || deser_headers->empty()) return;
            // Callers only pass headers whose nBits encode a valid target
            // (HasValidProofOfWork); the fuzz build's proof-of-work check does not.
            for (const auto& header : *deser_headers) {
                if (!DeriveTarget(header.nBits, params.powLimit)) return;
            }
            if (fuzzed_data_provider.ConsumeBool()) {
                // Make the headers connect and, optionally, carry the required
                // nBits, so the fuzzer explores accepted post-activation paths.
                uint256 prev_hash{all_headers.empty() ? chain_start->GetBlockHash() : all_headers.back().GetHash()};
                OrderedIndex scratch;
                const CBlockIndex* prev{presync ? presync_tip : nullptr};
                for (auto& header : *deser_headers) {
                    header.hashPrevBlock = prev_hash;
                    if (prev && fuzzed_data_provider.ConsumeBool()) header.nBits = RequiredBits(*prev, header, params);
                    if (prev) prev = &scratch.Append(header, prev, params);
                    prev_hash = header.GetHash();
                }
            }
            headers.swap(*deser_headers);
        } else if (auto num_headers_left{std::distance(redownloaded_it, all_headers.cend())}; num_headers_left > 0) {
            auto begin_it{redownloaded_it};
            std::advance(redownloaded_it, fuzzed_data_provider.ConsumeIntegralInRange<int>(1, num_headers_left));
            headers.insert(headers.cend(), begin_it, redownloaded_it);
        }
        if (headers.empty()) return;

        const auto result{headers_sync.ProcessNextHeaders(headers, fuzzed_data_provider.ConsumeBool())};
        requested_more = result.request_more;

        if (presync && result.success) {
            // Presync links headers by order and processed the whole batch.
            for (const auto& header : headers) {
                AssertValidDifficulty(*presync_tip, header, params);
                presync_tip = &index.Append(header, presync_tip, params);
            }
        }
        for (const auto& header : result.pow_validated_headers) {
            assert(header.hashPrevBlock == redownload_tip->GetBlockHash());
            AssertValidDifficulty(*redownload_tip, header, params);
            redownload_tip = &index.Append(header, redownload_tip, params);
        }

        if (result.request_more) {
            if (presync) {
                all_headers.insert(all_headers.cend(), headers.cbegin(), headers.cend());
                if (headers_sync.GetState() == HeadersSyncState::State::REDOWNLOAD) {
                    presync = false;
                    redownloaded_it = all_headers.cbegin();
                }
            }
            // NextHeadersRequestLocator() is covered by headers_sync_state; it
            // walks back to genesis, which this index does not contain.
        }
    }
}

