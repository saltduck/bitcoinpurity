// Copyright (c) 2026 The Bitcoin Purity developers
// Distributed under the MIT software license.
#include <mining/sv2_template.h>
#include <chainparams.h>
#include <common/args.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <interfaces/mining.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <node/miner.h>
#include <streams.h>
#include <sync.h>
#include <txmempool.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <validation.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

namespace mining {
namespace {
UniValue Failure(const std::string& reason)
{
    UniValue out{UniValue::VOBJ};
    out.pushKV("ok", false);
    out.pushKV("error", reason);
    return out;
}
template <typename T> std::string SerializeHex(const T& value)
{
    DataStream stream;
    stream << value;
    return HexStr(stream);
}
}
struct Sv2TemplateProvider::Impl {
    node::NodeContext& node;
    std::unique_ptr<interfaces::Mining> mining;
    std::atomic<bool> interrupted{false};
    std::mutex mutex;
    uint64_t next_id{0};
    bool headers_ahead{false};
    std::map<uint64_t, CBlock> templates;
    UniValue last;
    uint256 tip;
    uint64_t mempool_update{0};
    std::string outputs;
    std::chrono::steady_clock::time_point tip_seen{};
    std::chrono::steady_clock::time_point created{};
    explicit Impl(node::NodeContext& n) : node{n}, mining{interfaces::MakeMining(n)} {}
};
Sv2TemplateProvider::Sv2TemplateProvider(node::NodeContext& n) : m_impl{std::make_unique<Impl>(n)} {}
Sv2TemplateProvider::~Sv2TemplateProvider() = default;
void Sv2TemplateProvider::Interrupt()
{
    m_impl->interrupted = true;
    m_impl->mining->interrupt();
}
UniValue Sv2TemplateProvider::Request(const UniValue& request)
{
    auto& p = *m_impl;
    std::lock_guard lock{p.mutex};
    if (p.interrupted) return Failure("interrupted");
    try {
        const auto op = request["op"].get_str();
        if (op == "template") {
            const auto encoded = request["outputs"].get_str();
            if (encoded.size() > 131072 || !IsHex(encoded)) return Failure("invalid-outputs");
            DataStream stream{ParseHex(encoded)};
            std::vector<CTxOut> outputs;
            stream >> outputs;
            if (!stream.empty() || outputs.empty()) return Failure("invalid-outputs");
            uint64_t sigops{0};
            for (const auto& output : outputs) {
                if (output.nValue != 0) return Failure("nonzero-pool-output");
                sigops += uint64_t{output.scriptPubKey.GetSigOpCount(false)} * WITNESS_SCALE_FACTOR;
            }
            auto tip = p.mining->getTip();
            if (!tip) return Failure("not-ready");
            const auto now = std::chrono::steady_clock::now();
            if (p.tip != tip->hash) {
                p.tip = tip->hash;
                p.tip_seen = now;
            }
            if (!p.mining->isTestChain()) {
                if (p.mining->isInitialBlockDownload()) {
                    p.tip_seen = now;
                    return Failure("initial-sync");
                }
                int ahead{0};
                {
                    LOCK(cs_main);
                    if (p.node.chainman->m_best_header) ahead = p.node.chainman->m_best_header->nHeight - tip->height;
                }
                if (ahead > 0 && !p.headers_ahead) p.tip_seen = now;
                p.headers_ahead = ahead > 0;
                if (ahead > 0 && now - p.tip_seen < std::chrono::seconds{std::clamp(ahead, 3, 20)}) return Failure("catching-up");
            }
            const auto update = p.node.mempool->GetTransactionsUpdated();
            if (!p.last.isNull() && p.outputs == encoded && p.mempool_update == update &&
                p.last["previous_block_hash"].get_str() == HexStr(tip->hash) && now - p.created < std::chrono::seconds{30}) {
                if (!request["known_id"].isNull() && request["known_id"].getInt<uint64_t>() == p.last["id"].getInt<uint64_t>()) {
                    UniValue out{UniValue::VOBJ};out.pushKV("ok",true);out.pushKV("unchanged",true);return out;
                }
                return p.last;
            }
            node::BlockAssembler::Options options;
            node::ApplyArgsManOptions(*p.node.args, options);
            const uint64_t extra = GetSerializeSize(outputs);
            const uint64_t weight = 1168 + extra * WITNESS_SCALE_FACTOR;
            options.block_reserved_weight = std::max<uint64_t>(options.block_reserved_weight, weight);
            options.block_reserved_size = std::max<uint64_t>(options.block_reserved_size, (weight + 3) / 4 + 36);
            options.coinbase_output_max_additional_sigops = std::max<uint64_t>(options.coinbase_output_max_additional_sigops, sigops);
            if (options.block_reserved_weight > options.nBlockMaxWeight ||
                options.block_reserved_size > options.nBlockMaxSize ||
                options.coinbase_output_max_additional_sigops > MAX_BLOCK_SIGOPS_COST) return Failure("coinbase-resources-exceeded");
            auto tmpl = p.mining->createNewBlock2(options);
            if (!tmpl || p.interrupted) return Failure("interrupted");
            CBlock block = tmpl->getBlock();
            if (block.hashPrevBlock != tip->hash) return Failure("tip-changed");
            const auto id = ++p.next_id;
            UniValue out{UniValue::VOBJ};
            out.pushKV("ok", true);
            out.pushKV("id", id);
            out.pushKV("height", tip->height + 1);
            out.pushKV("version", uint64_t{static_cast<uint32_t>(block.nVersion)});
            out.pushKV("ntime", block.nTime);
            out.pushKV("nbits", block.nBits);
            out.pushKV("previous_block_hash", HexStr(block.hashPrevBlock));
            out.pushKV("coinbase", SerializeHex(TX_WITH_WITNESS(*block.vtx[0])));
            out.pushKV("size", GetSerializeSize(TX_WITH_WITNESS(block)));
            out.pushKV("weight", GetBlockWeight(block));
            out.pushKV("reward", block.vtx[0]->GetValueOut());
            out.pushKV("coinbase_prefix", HexStr(tmpl->getCoinbase().script_sig_prefix));
            UniValue transactions{UniValue::VARR}, ids{UniValue::VARR}, path{UniValue::VARR};
            for (size_t i = 1; i < block.vtx.size(); ++i) {
                transactions.push_back(SerializeHex(TX_WITH_WITNESS(*block.vtx[i])));
                ids.push_back(HexStr(block.vtx[i]->GetWitnessHash().ToUint256()));
            }
            for (const auto& hash : tmpl->getCoinbaseMerklePath()) path.push_back(HexStr(hash));
            out.pushKV("transactions", transactions);
            out.pushKV("wtxids", ids);
            out.pushKV("merkle_path", path);
            p.templates.emplace(id, std::move(block));
            while (p.templates.size() > 32) p.templates.erase(p.templates.begin());
            p.created = now;
            p.outputs = encoded;
            p.mempool_update = update;
            p.last = out;
            return out;
        }
        if (op == "validate" || op == "submit") {
            const auto it = p.templates.find(request["id"].getInt<uint64_t>());
            if (it == p.templates.end()) return Failure("unknown-template");
            CBlock block{it->second};
            CMutableTransaction coinbase;
            if (!DecodeHexTx(coinbase, request["coinbase"].get_str(), true, true) || coinbase.vin.size() != 1 || !coinbase.vin[0].prevout.IsNull()) return Failure("invalid-coinbase");
            block.vtx[0] = MakeTransactionRef(std::move(coinbase));
            const uint32_t version = request["version"].getInt<uint32_t>();
            if ((version & ~uint32_t{0x1fffe000}) != (static_cast<uint32_t>(block.nVersion) & ~uint32_t{0x1fffe000})) return Failure("invalid-version");
            block.nVersion = version;
            block.nTime = request["ntime"].getInt<uint32_t>();
            block.nNonce = request["nonce"].getInt<uint32_t>();
            block.hashMerkleRoot = BlockMerkleRoot(block);
            block.fChecked = block.m_checked_merkle_root = block.m_checked_witness_commitment = false;
            std::string reason, debug;
            bool valid;
            if (op == "submit") {
                valid = p.mining->submitBlock(block, reason, debug);
            } else {
                LOCK(cs_main);
                auto* parent = p.node.chainman->ActiveChain().Tip();
                if (parent->GetBlockHash() != block.hashPrevBlock) return Failure("stale-template");
                BlockValidationState state;
                valid = TestBlockValidity(state, p.node.chainman->GetParams(), p.node.chainman->ActiveChainstate(), block, parent, false, true);
                reason = state.GetRejectReason();
            }
            if (!valid) return Failure(reason.empty() ? "invalid-block" : reason);
            UniValue out{UniValue::VOBJ};
            out.pushKV("ok", true);
            out.pushKV("hash", block.GetHash().GetHex());
            return out;
        }
        return Failure("invalid-operation");
    } catch (const std::exception&) {
        return Failure("invalid-request");
    }
}
Sv2NodeCallbacks Sv2TemplateProvider::Callbacks()
{
    return {this, [](void* context, const uint8_t* data, size_t length, uint8_t** result, size_t* result_length) -> int32_t {
        if (!context || !data || !result || !result_length || length > 32 * 1024 * 1024) return -1;
        *result = nullptr;
        *result_length = 0;
        try {
            UniValue request;
            if (!request.read(std::string{reinterpret_cast<const char*>(data), length})) return -1;
            auto out = static_cast<Sv2TemplateProvider*>(context)->Request(request).write();
            auto* buffer = static_cast<uint8_t*>(std::malloc(out.size()));
            if (!buffer) return -1;
            std::memcpy(buffer, out.data(), out.size());
            *result = buffer;
            *result_length = out.size();
            return 0;
        } catch (...) { return -1; }
    }, [](uint8_t* buffer, size_t) { std::free(buffer); }};
}
}
